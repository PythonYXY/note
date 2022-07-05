#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#define NONE 0
#define READABLE 1
#define WRITABLE 2

#define OK 0
#define ERR -1


struct eventLoop;


// 事件处理函数
typedef void eventHandler(struct eventLoop *el, int fd, void *clientdata, int mask);


// 事件
typedef struct event {
    int mask;                // 感兴趣的事件类型
    eventHandler *rHandler;  // 读就绪事件处理函数
    eventHandler *wHandler;  // 写就绪事件处理函数
    void *clientdata;        // 客户端数据，传递给处理函数
}event;

// 就绪的事件
typedef struct firedEvent {
    int mask;      // 事件类型
    int fd;        // 文件描述符
}firedEvent;

// 事件循环结构体
typedef struct eventLoop {
    int maxsize;               // 可追踪的最大的文件描述符
    event *events;             // 一组感兴趣的事件，数组下标fd
    firedEvent *fired;         // I/O就绪的一组事件
    void *apidata;             // 保存I/O多路复用相关信息
    int stop;
} eventLoop;



// ----------- 与IO多路复用实现相关的封装函数 ------------------------------------------------------------------------------

typedef struct apiState {
    int epfd;
    struct epoll_event *events;
}apiState;

void apiCreate(eventLoop *el) {
    apiState *state = malloc(sizeof(apiState));

    state->events = malloc(sizeof(struct epoll_event)*el->maxsize);
    state->epfd = epoll_create(el->maxsize);

    if(state->epfd == ERR) {
        printf("create epoll instance failed! error code: %d\n", errno);
    }

    el->apidata = state;
}

void apiFree(eventLoop *el) {
    apiState *state = el->apidata;

    close(state->epfd);
    free(state->events);

    free(state);
}

int apiAddEvent(eventLoop *el, int fd, int mask) {

    apiState *state = el->apidata;

    struct epoll_event ee = {0};
    ee.events = NONE;
    mask |= el->events[fd].mask;

    if (mask & READABLE) ee.events |= EPOLLIN;
    if (mask & WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;


    int op = el->events[fd].mask == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;


    if (epoll_ctl(state->epfd, op, fd, &ee) == ERR) {
        printf("epoll_ctl add event failed!\n");
        return ERR;
    }
    return OK;
}

void apiDelEvent(eventLoop *el, int fd, int delmask) {
    apiState * state = el->apidata;

    struct epoll_event ee = {0};
    int mask = el->events[fd].mask & (~delmask);
    ee.events = 0;
    if (mask & READABLE) ee.events |= EPOLLIN;
    if (mask & WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;

    if(mask != NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}


int apiPoll(eventLoop *el) {
    apiState *state = el->apidata;
    int numReady, j;


    numReady = epoll_wait(state->epfd,state->events,el->maxsize,-1);


    if (numReady >= 0) {
        for (j = 0; j < numReady; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) mask |= READABLE;
            if (e->events & EPOLLOUT) mask |= WRITABLE;
            if (e->events & EPOLLERR) mask |= WRITABLE;
            if (e->events & EPOLLHUP) mask |= WRITABLE;
            el->fired[j].fd = e->data.fd;
            el->fired[j].mask = mask;

        }
    }
    return numReady;
}

// ---------------------------------------------------------------------------------------------------------------------




// ---------------- 事件循环、事件 相关操作 -------------------------------------------------------------------------------

eventLoop *createEventLoop(int setsize);  // 创建一个eventLoop实例

void deleteEventLoop(eventLoop *el);      // 销毁一个eventLoop实例

void startEventLoop(eventLoop *el);       // 启动一个eventLoop实例

void stopEventLoop(eventLoop *el);        // 停止事件循环

int addEvent(eventLoop *el, int fd, int mask, eventHandler *handler, void *clientdata);    // 注册事件

void deleteEvent(eventLoop *el, int fd, int mask);                // 删除事件

int processEvents(eventLoop *el);         // 处理事件



eventLoop *createEventLoop(int setsize) {
    eventLoop *el;
    el = malloc(sizeof(*el));
    el->events = malloc(sizeof(event)*setsize);
    el->fired = malloc(sizeof(firedEvent)*setsize);
    el->maxsize = setsize;
    apiCreate(el);
    return el;
}

void deleteEventLoop(eventLoop *el) {
    apiFree(el);
    free(el->events);
    free(el->fired);
    free(el);
}



int addEvent(eventLoop *el, int fd, int mask, eventHandler *handler, void *clientdata) {

    event *e = &el->events[fd];


    if(apiAddEvent(el, fd, mask) == ERR) return ERR;

    e->mask |= mask;

    if(e->mask & READABLE) e->rHandler = handler;
    if(e->mask & WRITABLE) e->wHandler = handler;
    e->clientdata = clientdata;

    return OK;
}

void deleteEvent(eventLoop *el, int fd, int delmask) {
    event *e = &el->events[fd];
    if(e->mask == NONE) return;

    apiDelEvent(el, fd, delmask);

    e->mask = e->mask & (~delmask);

}

int processEvents(eventLoop *el) {
    int processed=0, num;
    num = apiPoll(el);

    for(int j = 0; j < num; j++) {
        event *e = &el->events[el->fired[j].fd];
        int mask = el->fired[j].mask;
        int fd = el->fired[j].fd;
        int fired = 0;


        if(e->mask & mask & READABLE) {
            e->rHandler(el, fd, e->clientdata, mask);
        }

        if(e->mask & mask & WRITABLE) {
            e->rHandler(el, fd, e->clientdata, mask);
        }
        processed++;
    }
    return processed;
}

void startEventLoop(eventLoop *el) {
    while(el->stop != 1) {
        processEvents(el);
    }
}

void stopEventLoop(eventLoop *el) {
    el->stop = 1;
}

// ---------------------------------------------------------------------------------------------------------------------



// ------------------------------ demo ---------------------------------------------------------------------------------

void handler(eventLoop *el, int fd, void *data, int mask) {
    char buffer[1024] = {0};
    int n = read(fd, buffer, 1024);

    buffer[n - 1] = '\0';

    printf("fd: %d, mask: %d, clientdata: %s, read: %s\n", fd, mask, (char *) data, buffer);
    if (!strcmp(buffer, "stop")) {
        stopEventLoop(el);
        deleteEventLoop(el);
    }
}


int main() {
    eventLoop *el = createEventLoop(1024);

    if(!el) {
        printf("%s\n", "create eventLoop failed");
    }
    printf("%s\n", "create eventLoop success!");


    int n = addEvent(el, STDIN_FILENO, READABLE, handler, (void *) "stdin-readable-event");

    if(n != OK) {
        printf("%s\n", "add event failed!");
    }

    startEventLoop(el);
}


