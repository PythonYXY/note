# 1 欺诈检测

https://nightlies.apache.org/flink/flink-docs-release-1.14/zh/docs/try-flink/datastream/

在小额交易后的一分钟内出现大额交易将被判定为欺诈交易。

Main：

```java
package spendreport;

import org.apache.flink.streaming.api.datastream.DataStream;
import org.apache.flink.streaming.api.environment.StreamExecutionEnvironment;
import org.apache.flink.walkthrough.common.sink.AlertSink;
import org.apache.flink.walkthrough.common.entity.Alert;
import org.apache.flink.walkthrough.common.entity.Transaction;
import org.apache.flink.walkthrough.common.source.TransactionSource;

public class FraudDetectionJob {

    public static void main(String[] args) throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        DataStream<Transaction> transactions = env
            .addSource(new TransactionSource())
            .name("transactions");

        DataStream<Alert> alerts = transactions
            .keyBy(Transaction::getAccountId)
            .process(new FraudDetector())
            .name("fraud-detector");

        alerts
            .addSink(new AlertSink())
            .name("send-alerts");

        env.execute("Fraud Detection");
    }
}
```



FraudDetector:

```java
package spendreport;

import org.apache.flink.api.common.state.ValueState;
import org.apache.flink.api.common.state.ValueStateDescriptor;
import org.apache.flink.api.common.typeinfo.Types;
import org.apache.flink.configuration.Configuration;
import org.apache.flink.streaming.api.functions.KeyedProcessFunction;
import org.apache.flink.util.Collector;
import org.apache.flink.walkthrough.common.entity.Alert;
import org.apache.flink.walkthrough.common.entity.Transaction;

public class FraudDetector extends KeyedProcessFunction<Long, Transaction, Alert> {

    private static final long serialVersionUID = 1L;

    private static final double SMALL_AMOUNT = 1.00;
    private static final double LARGE_AMOUNT = 500.00;
    private static final long ONE_MINUTE = 60 * 1000;

    private transient ValueState<Boolean> flagState;
    private transient ValueState<Long> timerState;

    @Override
    public void open(Configuration parameters) {
        ValueStateDescriptor<Boolean> flagDescriptor = new ValueStateDescriptor<>(
                "flag",
                Types.BOOLEAN);
        flagState = getRuntimeContext().getState(flagDescriptor);

        ValueStateDescriptor<Long> timerDescriptor = new ValueStateDescriptor<>(
                "timer-state",
                Types.LONG);
        timerState = getRuntimeContext().getState(timerDescriptor);
    }

    @Override
    public void processElement(
            Transaction transaction,
            Context context,
            Collector<Alert> collector) throws Exception {

        // Get the current state for the current key
        Boolean lastTransactionWasSmall = flagState.value();

        // Check if the flag is set
        if (lastTransactionWasSmall != null) {
            if (transaction.getAmount() > LARGE_AMOUNT) {
                //Output an alert downstream
                Alert alert = new Alert();
                alert.setId(transaction.getAccountId());

                collector.collect(alert);
            }
            // Clean up our state
            cleanUp(context);
        }

        if (transaction.getAmount() < SMALL_AMOUNT) {
            // set the flag to true
            flagState.update(true);

            long timer = context.timerService().currentProcessingTime() + ONE_MINUTE;
            context.timerService().registerProcessingTimeTimer(timer);

            timerState.update(timer);
        }
    }

    @Override
    public void onTimer(long timestamp, OnTimerContext ctx, Collector<Alert> out) {
        // remove flag after 1 minute
        timerState.clear();
        flagState.clear();
    }

    private void cleanUp(Context ctx) throws Exception {
        // delete timer
        Long timer = timerState.value();
        ctx.timerService().deleteProcessingTimeTimer(timer);

        // clean up all state
        timerState.clear();
        flagState.clear();
    }
}
```



# 2 word count

https://juejin.cn/post/6844903972931649543

```java
public class WordCountJob  {

    public static void main(String[] args) throws Exception {

        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        DataStream<String> source = env.addSource(new WordCountSource())
                .name("SteveJobs-Word");

        DataStream<Tuple2<String,Integer>> proc = source.flatMap(new TextTokenizer())
                .keyBy(0)
                .sum(1)
                .name("Process-Word-Count");

        proc.addSink(new WordCountSink())
                .name("Word-Count-Sink");

        env.execute("Word Count");
    }

}

public class TextTokenizer implements FlatMapFunction<String, Tuple2<String, Integer>> {

    private static final long serialVersionUID = 2L;

    @Override
    public void flatMap(String str, Collector<Tuple2<String, Integer>> collector) throws Exception {
        if(str == null || str.isEmpty()){
            return;
        }

        String[] words = str.split("\\s+");

        for (String word : words ){
            if(word.contains(".") || word.contains(",") || word.contains("’s")){
                word = word.replaceAll(".|,|(’s)","");
            }
            word = word.trim();
            if(word.isEmpty()){
                continue;
            }
            if(!word.equals("I")){
                word = word.toLowerCase();
            }
            collector.collect(new Tuple2<>(word,1));
        }
    }

}
```



# 3 过滤

https://github.com/apache/flink-training/blob/release-1.14/ride-cleansing/src/solution/java/org/apache/flink/training/solutions/ridecleansing/RideCleansingSolution.java

过滤出起点和终点都在纽约市的路程

```java
public class RideCleansingSolution {

    private final SourceFunction<TaxiRide> source;
    private final SinkFunction<TaxiRide> sink;

    /** Creates a job using the source and sink provided. */
    public RideCleansingSolution(SourceFunction<TaxiRide> source, SinkFunction<TaxiRide> sink) {

        this.source = source;
        this.sink = sink;
    }

    /**
     * Main method.
     *
     * @throws Exception which occurs during job execution.
     */
    public static void main(String[] args) throws Exception {
        RideCleansingSolution job =
                new RideCleansingSolution(new TaxiRideGenerator(), new PrintSinkFunction<>());

        job.execute();
    }

    /**
     * Creates and executes the long rides pipeline.
     *
     * @return {JobExecutionResult}
     * @throws Exception which occurs during job execution.
     */
    public JobExecutionResult execute() throws Exception {

        // set up streaming execution environment
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        // set up the pipeline
        env.addSource(source).filter(new NYCFilter()).addSink(sink);

        // run the pipeline and return the result
        return env.execute("Taxi Ride Cleansing");
    }

    /** Keep only those rides and both start and end in NYC. */
    public static class NYCFilter implements FilterFunction<TaxiRide> {
        @Override
        public boolean filter(TaxiRide taxiRide) {
            return GeoUtils.isInNYC(taxiRide.startLon, taxiRide.startLat)
                    && GeoUtils.isInNYC(taxiRide.endLon, taxiRide.endLat);
        }
    }
}
```



# 4 rides-and-fares

https://github.com/apache/flink-training/blob/release-1.14/rides-and-fares/src/solution/java/org/apache/flink/training/solutions/ridesandfares/RidesAndFaresSolution.java

将相同rideId的ride和fare组合到一起

```java
public class RidesAndFaresSolution {

    private final SourceFunction<TaxiRide> rideSource;
    private final SourceFunction<TaxiFare> fareSource;
    private final SinkFunction<RideAndFare> sink;

    /** Creates a job using the sources and sink provided. */
    public RidesAndFaresSolution(
            SourceFunction<TaxiRide> rideSource,
            SourceFunction<TaxiFare> fareSource,
            SinkFunction<RideAndFare> sink) {

        this.rideSource = rideSource;
        this.fareSource = fareSource;
        this.sink = sink;
    }

    /**
     * Creates and executes the pipeline using the StreamExecutionEnvironment provided.
     *
     * @throws Exception which occurs during job execution.
     * @param env The {StreamExecutionEnvironment}.
     * @return {JobExecutionResult}
     */
    public JobExecutionResult execute(StreamExecutionEnvironment env) throws Exception {

        // A stream of taxi ride START events, keyed by rideId.
        DataStream<TaxiRide> rides =
                env.addSource(rideSource).filter(ride -> ride.isStart).keyBy(ride -> ride.rideId);

        // A stream of taxi fare events, also keyed by rideId.
        DataStream<TaxiFare> fares = env.addSource(fareSource).keyBy(fare -> fare.rideId);

        // Create the pipeline.
        rides.connect(fares)
                .flatMap(new EnrichmentFunction())
                .uid("enrichment") // uid for this operator's state
                .name("enrichment") // name for this operator in the web UI
                .addSink(sink);

        // Execute the pipeline and return the result.
        return env.execute("Join Rides with Fares");
    }

    /** Creates and executes the pipeline using the default StreamExecutionEnvironment. */
    public JobExecutionResult execute() throws Exception {
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        return execute(env);
    }

    /**
     * Main method.
     *
     * @throws Exception which occurs during job execution.
     */
    public static void main(String[] args) throws Exception {

        RidesAndFaresSolution job =
                new RidesAndFaresSolution(
                        new TaxiRideGenerator(),
                        new TaxiFareGenerator(),
                        new PrintSinkFunction<>());

        // Setting up checkpointing so that the state can be explored with the State Processor API.
        // Generally it's better to separate configuration settings from the code,
        // but for this example it's convenient to have it here for running in the IDE.
        Configuration conf = new Configuration();
        conf.setString("state.backend", "filesystem");
        conf.setString("state.checkpoints.dir", "file:///tmp/checkpoints");
        conf.setString("execution.checkpointing.interval", "10s");
        conf.setString(
                "execution.checkpointing.externalized-checkpoint-retention",
                "RETAIN_ON_CANCELLATION");

        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment(conf);

        job.execute(env);
    }

    public static class EnrichmentFunction
            extends RichCoFlatMapFunction<TaxiRide, TaxiFare, RideAndFare> {

        private ValueState<TaxiRide> rideState;
        private ValueState<TaxiFare> fareState;

        @Override
        public void open(Configuration config) {

            rideState =
                    getRuntimeContext()
                            .getState(new ValueStateDescriptor<>("saved ride", TaxiRide.class));
            fareState =
                    getRuntimeContext()
                            .getState(new ValueStateDescriptor<>("saved fare", TaxiFare.class));
        }

        @Override
        public void flatMap1(TaxiRide ride, Collector<RideAndFare> out) throws Exception {

            TaxiFare fare = fareState.value();
            if (fare != null) {
                fareState.clear();
                out.collect(new RideAndFare(ride, fare));
            } else {
                rideState.update(ride);
            }
        }

        @Override
        public void flatMap2(TaxiFare fare, Collector<RideAndFare> out) throws Exception {

            TaxiRide ride = rideState.value();
            if (ride != null) {
                rideState.clear();
                out.collect(new RideAndFare(ride, fare));
            } else {
                fareState.update(fare);
            }
        }
    }
}

```



# 5 Windowed Analytics (Hourly Tips)

https://github.com/apache/flink-training/tree/release-1.14/hourly-tips

统计每个小时内拥有最大tips的driverId

```java
public class HourlyTipsSolution {

    private final SourceFunction<TaxiFare> source;
    private final SinkFunction<Tuple3<Long, Long, Float>> sink;

    /** Creates a job using the source and sink provided. */
    public HourlyTipsSolution(
            SourceFunction<TaxiFare> source, SinkFunction<Tuple3<Long, Long, Float>> sink) {

        this.source = source;
        this.sink = sink;
    }

    /**
     * Main method.
     *
     * @throws Exception which occurs during job execution.
     */
    public static void main(String[] args) throws Exception {

        HourlyTipsSolution job =
                new HourlyTipsSolution(new TaxiFareGenerator(), new PrintSinkFunction<>());

        job.execute();
    }

    /**
     * Create and execute the hourly tips pipeline.
     *
     * @return {JobExecutionResult}
     * @throws Exception which occurs during job execution.
     */
    public JobExecutionResult execute() throws Exception {

        // set up streaming execution environment
        StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

        // start the data generator and arrange for watermarking
        DataStream<TaxiFare> fares =
                env.addSource(source)
                        .assignTimestampsAndWatermarks(
                                // taxi fares are in order
                                WatermarkStrategy.<TaxiFare>forMonotonousTimestamps()
                                        .withTimestampAssigner(
                                                (fare, t) -> fare.getEventTimeMillis()));

        // compute tips per hour for each driver
        DataStream<Tuple3<Long, Long, Float>> hourlyTips =
                fares.keyBy((TaxiFare fare) -> fare.driverId)
                        .window(TumblingEventTimeWindows.of(Time.hours(1)))
                        .process(new AddTips());

        // find the driver with the highest sum of tips for each hour
        DataStream<Tuple3<Long, Long, Float>> hourlyMax =
                hourlyTips.windowAll(TumblingEventTimeWindows.of(Time.hours(1))).maxBy(2);

        /* You should explore how this alternative (commented out below) behaves.
         * In what ways is the same as, and different from, the solution above (using a windowAll)?
         */

//         DataStream<Tuple3<Long, Long, Float>> hourlyMax = hourlyTips.keyBy(t -> t.f0).maxBy(2);

        hourlyMax.addSink(sink);

        // execute the transformation pipeline
        return env.execute("Hourly Tips");
    }

    /*
     * Wraps the pre-aggregated result into a tuple along with the window's timestamp and key.
     */
    public static class AddTips
            extends ProcessWindowFunction<TaxiFare, Tuple3<Long, Long, Float>, Long, TimeWindow> {

        @Override
        public void process(
                Long key,
                Context context,
                Iterable<TaxiFare> fares,
                Collector<Tuple3<Long, Long, Float>> out) {

            float sumOfTips = 0F;
            for (TaxiFare f : fares) {
                sumOfTips += f.tip;
            }
            out.collect(Tuple3.of(context.window().getEnd(), key, sumOfTips));
        }
    }
}

```

