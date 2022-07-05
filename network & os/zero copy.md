本文已参与「[掘力星计划](https://juejin.cn/post/7012210233804079141/ "https://juejin.cn/post/7012210233804079141/")」，赢取创作大礼包，挑战创作激励金。

> 详细介绍了两种zero-copy零拷贝技术mmap和sendfile的概念和基本原理。

很多软件是基于server-client模式的，最常见的下载功能需要从Server端的磁盘中将文件通过网络发送到客户端中去。如果采用传统标准IO的方式（基于数据拷贝），那么需要如下步骤：

![](zero copy.assets/01af5f8d07064a8f96a0a95260dd7caf~tplv-k3u1fbpfcp-zoom-in-crop-mark:4536:0:0:0.awebp)

传统标准IO通过网络传输数据，需要进行如下调用：

```java
buffer = File.read 
Socket.send(buffer)
```

**总共需要四步：** 

1.  **read()：涉及到两次上下文切换以及两次数据拷贝；**
    1.  读取磁盘文件，将数据DMA Copy到操作系统内核缓冲区Page Cache；
    2.  将内核缓冲区Page Cache的数据，CPU Copy到应用程序缓存；
2.  **send()：涉及到两次上下文切换以及两次数据拷贝；**
    1.  将应用程序缓存中的数据，CPU Copy到socket网络发送缓冲区，即Socket Cache；
    2.  将Socket Cache的数据，DMA Copy到网卡，由网卡进行网络传输。

**可以发现，完成一次读写，需要4此上下文切换、2次DMA数据拷贝、两次CPU数据拷贝，实际上，如果仅仅是数据传输，那么数据根本不需要经过这么多次的拷贝。** 

**DMA：Direct Memory Access ，它可以独立地直接读写系统内存，不需要 CPU 介入，像显卡、网卡之类都会用DMA。** 

**零拷贝（Zero-copy）技术是指计算机执行操作时，CPU不需要先将数据从某处内存复制到另一个特定区域。这种技术通常用于通过网络传输数据时节省CPU周期和内存带宽。零拷贝技术可以减少数据拷贝和共享总线操作的次数，消除传输数据在存储器之间不必要的中间拷贝次数，从而有效地提高数据传输效率。而且，零拷贝技术减少了用户进程地址空间和内核地址空间之间因为上下文切换而带来的开销。** 

**==常见的零拷贝技术分类：==**

1.  **直接 I/O**：数据直接跨过内核缓冲区，在用户地址空间与 I/O 设备之间传递，内核只是进行必要的虚拟存储配置等辅助工作；
2.  **数据传输不经过用户空间**：当应用程序在数据传输过程中不需要对数据进行访问时，则可以避免将数据从内核空间到用户空间之间的拷贝，传输的数据在页缓存中就可以得到处理；Linux 中提供类似的系统调用主要有 mmap()，sendfile() 以及 splice()。
3.  **写时复制**：数据不需要提前拷贝，而是当需要修改的时候再进行部分拷贝。COW是对数据在 Linux 的页缓存和用户进程的缓冲区之间的传输过程进行优化手段。

**下面介绍数据传输不经过用户空间的零拷贝技术：mmap和sendfile，这也是Netty、Kafka、RocketMQ等框架所使用的底层技术。** 

2.1 sendfile调用
--------------

**Linux 在版本 2.1 中引入了 `sendfile()` 这个系统调用，sendfile()是一种零拷贝的实现。Java对sendfile的支持就是NIO中的`FileChannel.transferTo()`或者`transferFrom()`。**  ![](zero copy.assets/a6bd49fffa0c4b2d8d83877c0e5a8a05~tplv-k3u1fbpfcp-zoom-in-crop-mark:4536:0:0:0.awebp)

**使用sendfile进行网络数据传输流程为：** 

1.  发起sendfile() 系统调用，上下文切换一次。将文件中的数据DMA Copy到Page Cache中。
2.  继续将Page Cache中的数据CPU Copy到与 Scocket Cache中去。
3.  继续将Scocket Cache的数据DMA Copy到网卡，由网卡进行网络传输。sendfile() 系统调用返回，上下文切换一次。

**可以看到整个流程，减少了一次CPU Copy，减少了两次的上下文切换，相比于传统IO确实提升了性能。** 

但是，数据仍旧需要一次从Page Cache到Socket Cache的CPU Copy，这个Copy能不能也去掉呢？

**当然可以，Linux 2.4+ 版本之后，文件描述符结果被改变，借助DMA Gather(带有收集功能的DMA)，sendfile()再次减少了一次 Copy 操作，变成了真正的零拷贝（没有CPU Copy）。** 

此时整个步骤变为：

1.  发起sendfile() 系统调用，上下文切换一次。将文件中的数据DMA Copy到Page Cache中。
2.  继续将Page Cache中的带有文件位置和长度信息的缓冲区描述符CPU Copy到Socket Cache中去，这部分拷贝很少的数据，可忽略。
3.  继续借助DMA Gather ，直接将Scocket Cache的真正数据DMA Copy到网卡，由网卡进行网络传输。这样就避免了最后一次CPU Copy。sendfile() 系统调用返回，上下文切换一次。

**sendfile + DMA Gather流程如下：** 

![](zero copy.assets/52258edb41024a40bf3b9a579c9abd1c~tplv-k3u1fbpfcp-zoom-in-crop-mark:4536:0:0:0.awebp)

**sendfile + DMA Gather，使得整个传输只需要两次上下文切换，数据只需要两次DMA Copy，降低了上下文切换和数据拷贝带来的开销，极大的提升了数据传输的效率，没有CUP拷贝，是真正的零拷贝。** 

**==但是，sendfile调用有一个缺点，那就是无法在sendfile调用过程中修改数据，因此sendfile()只是适用于应用程序地址空间不需要对所访问数据进行处理的和修改情况，常见的就是文件传输，或者MQ消费消息的获取，如果想要在传输过程中修改数据，可以使用mmap系统调用。==** 

**mmap调用是一个比sendfile调用昂贵但优于传统I/O的零拷贝实现方式，而mmap调用则可以在中途直接修改Page Cache中的数据，这也是mmap零拷贝的优点。** 

2.1 mmap调用
----------

**mmap（Memory Mapped Files）是一种零拷贝技术，学名内存映射文件，Java中的实现就是`MappedByteBuffer`，通过`channel#map`方法得到。** 

mmap将一个文件(或者文件的一部分)映射到进程的地址空间，实现文件磁盘地址和进程虚拟地址空间中一段虚拟地址的一一对映关系。注意这时候没有分配和映射到具体的物理内存空间，而是到第一次加载这个文件的时候，通过MMU把之前虚拟地址换算成物理地址，把文件加载进物理内存——内核空间的Page Cache中。

实现这样的映射关系后，进程就可以采用指针的方式读写操作这一段内存，而系统会自动回写脏页面到对应的文件磁盘上，即完成了对文件的操作而不必再调用 read，write 等系统调用函数。相反，内核空间对这段区域的修改也直接反映用户空间，从而可以实现不同进程间的文件共享。

**简单的说，使用mmap之后，数据无需拷贝到用户空间中，应用程序可以直接操作Page Cache中的数据。** 

mmap()代替read()调用之后的数据发送流程为：

```java
buf = mmap(file, len);
write(sockfd, buf, len);
```

使用mmap技术之后，数据流转图如下：

![](zero copy.assets/6852f9e6fca94285bbb8d55c60b3081c~tplv-k3u1fbpfcp-zoom-in-crop-mark:4536:0:0:0.awebp)

此时整个步骤变为：

1.  **mmap()：涉及到两次上下文切换以及一次数据拷贝；**
    1.  发出mmap系统调用，发生一次上下文切换。通过DMA引擎将磁盘文件中的内容拷贝到内核空间的一个缓冲区（Page Cache）中。
    2.  mmap系统调用返回，发生一次上下文切换。此后用户空间和内核空间共享这个缓冲区，用户空间就可以像在操作自己缓冲区中数据一般操作这个由内核空间共享的缓冲区数据，而不需要将数据在内核空间和用户空间之间来回拷贝。
2.  **write()：涉及到两次上下文切换以及两次数据拷贝；**
    1.  发起write调用，发生一次上下文切换。将缓冲区（Page Cache）的内容CPU Copy到Socket Cache。
    2.  将Socket Cache的数据，DMA Copy到网卡，由网卡进行网络传输。write调用返回，发生一次上下文切换。

**这种mmap+write的方式相比于传统IO少了一次CPU Copy，从而极大地提高了效率。虽然性能弱于sendfile零拷贝，但其好处是可以在中途修改内存中的数据之后再传输。** 

**另外，当应用程序往 mmap 输出数据时，此时就直接输出到了内核态的缓冲区数据，如果此时输出设备是磁盘的话，不会立即写磁盘，linux系统下通常会间隔是30秒由操作系统自动落盘，也可手动调用fsync()函数让其立即落盘，实现真正的持久化。** 

2.2 MQ中的应用
----------

对于Kafka来说：

1.  数据从Producer到Broker，需要将来自网卡的消息持久化的磁盘中，Kafka中采用**mmap**的方式写，并且不会立即持久化到磁盘中，而是存入page cache内核缓冲区中就直接返回成功。后续有消费者来拉取消息的时候，也是先冲缓冲区中查找消息，如果有就直接发送给消费者，不会再查找磁盘，又提升了拉消息的性能。实际上它的日志文件并没有用到 mmap，而索引文件用了 mmap。
2.  数据从Broker到Consumer，需要将磁盘中的消息通过网卡发送出去，Kafka中采用**sendfile**的方式，将磁盘文件读到OS内核缓冲区后，直接转到socket buffer进行网络发送。

对于rocketMQ来说，如论是消息存储还是消费，都是采用**mmap**的方式，并且通过预热来减少大文件 mmap 因为缺页中断产生的性能问题。

**参考资料：** 

1.  [Kafka零拷贝](https://link.juejin.cn/?target=https%3A%2F%2Fzhuanlan.zhihu.com%2Fp%2F78335525 "https://zhuanlan.zhihu.com/p/78335525")
2.  [零拷贝（Zero-copy）及其应用详解](https://link.juejin.cn/?target=https%3A%2F%2Fwww.jianshu.com%2Fp%2F193cae9cbf07 "https://www.jianshu.com/p/193cae9cbf07")
3.  [Kafka和RocketMQ底层存储之那些你不知道的事](https://link.juejin.cn/?target=https%3A%2F%2Fzhuanlan.zhihu.com%2Fp%2F163759210 "https://zhuanlan.zhihu.com/p/163759210")

> 如有需要交流，或者文章有误，请直接留言。另外希望点赞、收藏、关注，我将不间断更新各种Java学习博客！