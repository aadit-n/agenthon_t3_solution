#pragma once
#define T3_STANDALONE
#include "core.hpp"
#include "random.hpp"
#include "latency_stream.hpp"
#include "action_stream.hpp"
#include "json_view.hpp"
#include <memory>
#include <string>
#include <optional>

namespace t3 {
using Json=JsonView;
constexpr i64 DATE=1612483200000000000LL;

// The reference mixes integer and floating timestamps around Poisson shocks.
// Keep that distinction: converting every timestamp to double loses nanoseconds.
struct OracleTime {
    i64 integer=0;
    double real=0;
    bool floating=false;
    OracleTime(i64 value):integer(value) {}
    OracleTime(double value):real(value),floating(true) {}
    double as_double() const { return floating?real:double(integer); }
    double minus(const OracleTime& other) const {
        return !floating&&!other.floating?double(integer-other.integer):as_double()-other.as_double();
    }
};
class Oracle {
    Random& global;
    Random rng;
    OracleTime previous;
    i64 value,reference;
    double gamma,theta,rate,shock_mean,shock_time,shock_value,variance_coefficient;
    bool jump_pending=false;
    i64 jump_time=0,jump_value=0;
    i64 compute(OracleTime time,double adjustment) {
        double d=time.minus(previous);
        double loc=reference+(value-reference)*std::exp(-gamma*d);
        double scale=std::sqrt(variance_coefficient*(1-std::exp(-2*gamma*d)));
        double v=rng.normal(loc,scale)+adjustment;
        value=rounded_integer(std::max(0.,v));
        if (jump_pending && (time.floating?time.real>=double(jump_time):time.integer>=jump_time)) {
            value=std::max<i64>(0,value+jump_value); jump_pending=false;
        }
        previous=time;
        return value;
    }
    double shock() {
        double v=rng.normal(shock_mean,std::sqrt(50000.));
        return rng.bounded(2)==0?v:-v;
    }
public:
    Oracle(const Json& p,Random& source):global(source),rng(source.word()),
        previous(DATE+34200000000000LL),value(p.value("initial_price",i64(100000))),reference(value) {
        double k=p.value("kappa",0.);gamma=k>0?k/1e9:1.67e-16;
        double r=p.value("jump_intensity",0.);rate=r>0?r/1e9:2.77778e-18;
        theta=p.value("sigma",5e-5);shock_mean=p.value("jump_sigma",0.);
        variance_coefficient=std::pow(theta,2)/(2*gamma);
        if (!shock_mean) shock_mean=1000;
        if (p.contains("scheduled_jump")&&!p["scheduled_jump"].is_null()&&!p["scheduled_jump"].empty()) {
            const auto& j=p["scheduled_jump"];jump_pending=true;
            jump_time=previous.integer+j.at("time_ns").get<i64>();jump_value=j.at("magnitude").get<i64>();
        }
        shock_time=double(previous.integer)+global.exponential(1/rate);shock_value=shock();
    }
    i64 advance(i64 time) {
        if (time<=previous.integer&&!previous.floating) return value;
        if (previous.floating && double(time)<=previous.real) return value;
        while (shock_time<double(time)) {
            compute(OracleTime(shock_time),shock_value);
            shock_time=previous.as_double()+double(i64(global.exponential(1/rate)));
            shock_value=shock();
        }
        return compute(OracleTime(time),0);
    }
    i64 observe(i64 time,Random& observer,double variance) {
        i64 closing=DATE+57600000000000LL;
        i64 v=advance(time>=closing?closing-1:time);
        return variance?rounded_integer(observer.normal(double(v),std::sqrt(variance))):v;
    }
};

class Simulation {
    Random global;
    Oracle oracle;
    std::vector<Random> rngs;
    Random latency_rng;
    int latency_kind=4;
    double latency_mu=0,latency_sigma=0,latency_min=0,latency_max=1e12,latency_alpha=1.5;
    i64 fixed_latency=0;
    std::vector<double> locations;
    std::vector<i64> exchange_latencies;
    std::unique_ptr<LatencyStream> latency_stream;
    std::unique_ptr<ActionStream> action_stream;
    bool action_latency=false;
    NoiseAction noise_action(int agent) {
        const auto& a=c.agents[agent];
        i64 size=std::max<i64>(1,rounded_integer(rngs[agent].normal(a.mean,a.stddev)));
        int buy=rngs[agent].bounded(2);
#ifdef T3_PREPARED_RANDOM
        i64 offset=rngs[agent].bounded_prepared(a.offset,a.offset_mask);
#else
        i64 offset=rngs[agent].bounded(a.offset+1);
#endif
        return {size,offset,buy};
    }
    i64 sampled_latency() {
        double v=latency_kind==1?latency_rng.uniform(latency_min,latency_max):
                 latency_kind==2?latency_rng.lognormal(latency_mu,latency_sigma):
                 (latency_min>0?latency_min:1)*(1+latency_rng.pareto(latency_alpha));
        return rounded_integer(std::max(latency_min,std::min(latency_max,v)));
    }
    i64 latency_for(int sender,int recipient) {
        if (sender==recipient) return 0;
        if (latency_kind==4) return fixed_latency;
        if (latency_kind==0) {
            if(sender==0||recipient==0) return exchange_latencies[sender==0?recipient:sender];
            double distance=locations[sender]-locations[recipient];
            return i64(std::sqrt(distance*distance)/0.299792458);
        }
        return action_latency?action_stream->next_latency():latency_stream?latency_stream->next():sampled_latency();
    }
    void send(int sender,int recipient,Msg&& msg) {
        msg.src=sender;msg.dst=recipient;msg.parent=c.causal;
        msg.send=c.now+c.agents[sender].delay;
        if (sender==0&&(msg.kind==ACCEPTED||msg.kind==EXECUTED||msg.kind==CANCELLED)) msg.send+=c.ack_delay;
        msg.arrive(latency_for(sender,recipient));c.enqueue(std::move(msg));
    }
    void send(int sender,int recipient,const Msg& msg) { Msg copy=msg; send(sender,recipient,std::move(copy)); }
    void simple(int sender,int recipient,int kind) { send(sender,recipient,c.make(kind)); }
    void order_message(int sender,int kind,const Order& order) {
        Msg msg=c.make(kind);msg.order=order;send(sender,sender?0:order.agent,std::move(msg));
    }
    void place(int agent,i64 size,int side,i64 price) {
        Order o;o.id=c.next_order++;o.agent=agent;o.quantity=size;o.side=side;o.price=price;
        c.remember(agent,o);order_message(agent,LIMIT,o);c.log(agent,SUBMITTED,o);
    }
    void act(int agent,i64 bid,i64 ask) {
        auto& a=c.agents[agent];
        if (a.type==1) {
            NoiseAction action=action_stream?action_stream->next_noise(agent):noise_action(agent);
            i64 size=action.size,offset=action.offset;int buy=action.buy;
            i64 anchor=buy?(ask?ask:(bid?bid:a.reference)):(bid?bid:(ask?ask:a.reference));
            place(agent,size,buy?0:1,buy?anchor+offset:anchor-offset);
        } else if (a.type==2) {
            i64 mid=bid&&ask?(bid+ask)/2:a.reference;
            for (const auto& item:a.open) order_message(agent,CANCEL,item.second);
            for (int level=0;level<a.depth;++level) {
                place(agent,a.size,0,mid-a.spread/2-level);place(agent,a.size,1,mid+a.spread/2+level);
            }
        } else {
            if (!bid&&!ask) return;
            double mid=bid&&ask?(double(bid)+double(ask))/2:double(bid?bid:ask);
            i64 size=std::max<i64>(1,rounded_integer(a.mean));
            if (a.type==3) {
                i64 fundamental=oracle.observe(c.now,rngs[agent],a.sigma_n);
                if (mid<fundamental-a.threshold&&ask) place(agent,size,0,ask);
                else if (mid>fundamental+a.threshold&&bid) place(agent,size,1,bid);
            } else {
                if (a.mids.empty()) a.mids.resize(size_t(a.lookback+1),0.0);
                a.mids[a.mid_head]=mid;
                a.mid_head=(a.mid_head+1)%size_t(a.lookback+1);
                ++a.mid_count;
                if (a.mid_count<=size_t(a.lookback)) return;
                double past=a.mids[a.mid_head];
                if (mid>past+a.threshold&&ask) place(agent,size,0,ask);
                else if (mid<past-a.threshold&&bid) place(agent,size,1,bid);
            }
        }
    }
    inline __attribute__((always_inline)) void limit(Order order) {
        if (__builtin_expect(order.quantity<=0||order.price<0, 0)) return;
        while (true) {
            if (c.book.cross(order)) {
                Order resting=c.book.front(order.side);
                if (__builtin_expect(c.stp&&resting.agent==order.agent, 0)) {
                    if (c.stp==1) { resting=c.book.cancel(resting);order_message(0,CANCELLED,resting);continue; }
                    order_message(0,CANCELLED,order);break;
                }
                resting=c.book.take(order.side,order.quantity);
                Order filled=order;filled.quantity=resting.quantity;filled.fill_price=resting.fill_price;
                order.quantity-=filled.quantity;order_message(0,EXECUTED,resting);order_message(0,EXECUTED,filled);
                if (!order.quantity) break;
            } else { c.book.enter(order);order_message(0,ACCEPTED,order);break; }
        }
        c.quotes();
    }
    inline __attribute__((always_inline)) void exchange(const Msg& msg) {
        c.agents[0].delay=c.compute_delay;
        if (__builtin_expect(c.now>c.close, 0)&&msg.kind!=QUERY) { simple(0,msg.src,CLOSED);return; }
        switch (msg.kind) {
        case QUERY: {
            Msg response=c.make(SPREAD);response.spread(c.book.best_price(0),c.book.best_price(1),c.now>c.close);
            send(0,msg.src,std::move(response));break;
        }
        case LIMIT:limit(msg.order);break;
        case CANCEL: { Order o=c.book.cancel(msg.order);if (o.id>=0) order_message(0,CANCELLED,o);break; }
        case HOURS_REQ:c.agents[0].delay=0;simple(0,msg.src,HOURS);break;
        case CLOSE_REQ:c.subscriptions.push_back(msg.src);break;
        default:throw std::runtime_error("unexpected exchange message");
        }
    }
    inline __attribute__((always_inline)) void receive(const Msg& msg) {
        auto& a=c.agents[msg.dst];
        switch (msg.kind) {
        case SPREAD:
            if (__builtin_expect(msg.spread_closed(), 0)) a.closed=true;
            if (a.awaiting) { if (__builtin_expect(!a.closed, 1)) act(msg.dst,msg.spread_bid(),msg.spread_ask());a.awaiting=false; } break;
        case EXECUTED:c.log(msg.dst,EXEC,msg.order);c.executed(msg.dst,msg.order);break;
        case ACCEPTED:c.log(msg.dst,ACCEPT,msg.order);break;
        case CANCELLED:c.log(msg.dst,CANCEL_EVENT,msg.order);c.cancelled(msg.dst,msg.order.id);break;
        case HOURS:
            if (!a.hours) { a.hours=true;if (c.open<c.now) throw std::runtime_error("wakeup in past");c.wake(msg.dst,c.open); } break;
        case CLOSED:a.closed=true;break;
        case CLOSE_PRICE:break;
        default:throw std::runtime_error("unexpected agent message");
        }
    }
    static int count(const Json& scenario) {
        int total=1;for (const auto& group:scenario.at("agent_configs")) total+=group.at("count").get<int>();return total;
    }
public:
    Core c;
    explicit Simulation(const Json& scenario,std::optional<uint32_t> override_seed={},bool parallel_latency=false):
        global(override_seed.value_or(scenario.at("seed").get<uint32_t>())),
        oracle(scenario.at("oracle_config").value("params",Json()),global),latency_rng(0),c(count(scenario)) {
        c.now=DATE;c.open=DATE+34200000000000LL;c.close=c.open+scenario.at("horizon_ns").get<i64>();c.stop=c.close+1000000000;
        const auto& ex=scenario.at("exchange_config");
        if (ex.value("protocol_enforcement",false)) {
            c.ack_delay=ex.value("ack_delay_ns",i64(0));c.compute_delay=ex.value("compute_delay_ns",i64(0));
            std::string stp=ex.contains("stp_policy")&&!ex["stp_policy"].is_null()?ex["stp_policy"].get<std::string>():std::string();
            c.stp=stp=="cancel_oldest"?1:(!stp.empty()?2:0);
        }
        i64 reference=scenario.at("oracle_config").value("params",Json()).value("initial_price",i64(100000));
        rngs.reserve(c.agents.size());rngs.emplace_back(global.word());int index=1;
        for (const auto& group:scenario.at("agent_configs")) {
            std::string kind=group.at("agent_type").get<std::string>();const Json p=group.value("params",Json());
            AgentState a;a.time=c.now;a.reference=reference;
            double hz=p.value("arrival_rate_hz",1.);
            a.interval=std::max<i64>(1,p.contains("rebalance_interval_ns")?p["rebalance_interval_ns"].get<i64>():hz>0?i64(1e9/hz):1000000000);
            if (kind=="NoiseTrader") {
                a.type=1;a.mean=p.value("order_size_mean",10.);a.stddev=p.value("order_size_std",2.);a.offset=p.value("price_offset_ticks",i64(5));
                if (a.stddev<0||a.offset<0) throw std::invalid_argument("negative noise scale/offset");
                a.offset_mask=Random::mask_for(a.offset);
            } else if (kind=="MarketMaker") {
                a.type=2;a.spread=std::max(2,p.value("spread_ticks",2));a.depth=std::max(1,p.value("depth_levels",3));a.size=std::max<i64>(1,p.value("size_per_level",i64(10)));
            } else if (kind=="ValueTrader") { a.type=3;a.mean=p.value("order_size_mean",25.);a.threshold=p.value("threshold_ticks",i64(2));a.sigma_n=1000; }
            else if (kind=="MomentumTrader") { a.type=4;a.mean=p.value("order_size_mean",15.);a.threshold=p.value("threshold_ticks",i64(2));a.lookback=std::max(1,p.value("lookback",5)); }
            else throw std::invalid_argument("unsupported agent type: "+kind);
            for (int n=0;n<group.at("count").get<int>();++n) { c.agents[index++]=a;rngs.emplace_back(global.word()); }
        }
        c.agents[0].time=c.now;
        // Reserve from the configured activity rate, never from a scenario ID
        // or reference data. Cap the estimate; vectors still grow if necessary.
        if (std::getenv("T3SIM_NO_RESERVE")==nullptr) {
            long double trace_estimate=0,ledger_estimate=0;
            for (size_t i=1;i<c.agents.size();++i) {
                const auto& a=c.agents[i];long double actions=1+static_cast<long double>(c.close-c.open)/a.interval;
                trace_estimate+=actions*(a.type==2?8*a.depth:6);
                ledger_estimate+=actions*(a.type==2?8*a.depth+6:8);
            }
            c.trace.reserve(size_t(std::max(65536.L,std::min(262144.L,trace_estimate))));
            c.ledger.reserve(size_t(std::max(65536.L,std::min(262144.L,ledger_estimate))));
        }
        latency_rng=Random(global.word());
        if (scenario.contains("latency_config")&&!scenario["latency_config"].is_null()&&!scenario["latency_config"].empty()) {
            auto latency=scenario["latency_config"];Json p=latency.value("params",Json());
            std::string model=latency.value("model",std::string("deterministic"));
            latency_kind=model=="uniform"?1:model=="log_normal"?2:model=="pareto"?3:4;
            double mean=p.value("mean_ns",0.);latency_mu=mean>0?std::log(mean):0;
            latency_sigma=p.value("sigma",0.);latency_min=p.value("min_ns",0.);latency_max=p.value("max_ns",1e12);latency_alpha=p.value("alpha",1.5);
            fixed_latency=rounded_integer(std::max(latency_min,std::min(latency_max,mean)));
        } else {
            latency_kind=0;locations.reserve(c.agents.size());
            for (size_t i=0;i<c.agents.size();++i) locations.push_back(latency_rng.uniform(0,3866660));
            exchange_latencies.reserve(locations.size());
            for(double position:locations) {double distance=locations[0]-position;exchange_latencies.push_back(i64(std::sqrt(distance*distance)/0.299792458));}
        }
        global.word(); // The otherwise unused kernel RandomState seed.
        const char* noise_prefetch=std::getenv("T3SIM_NOISE_PREFETCH");
        long double noise_workload=0;std::vector<int> noise_ids;
        for(size_t i=1;i<c.agents.size();++i) if(c.agents[i].type==1) {noise_workload+=1+static_cast<long double>(c.close-c.open)/c.agents[i].interval;noise_ids.push_back(int(i));}
        bool prepare_noise=parallel_latency&&noise_prefetch&&std::strcmp(noise_prefetch,"off")!=0&&!noise_ids.empty()&&(std::strcmp(noise_prefetch,"auto")!=0||noise_workload>=65536);
        const char* prefetch=std::getenv("T3SIM_LATENCY_PREFETCH");
        if(parallel_latency&&prefetch&&std::strcmp(prefetch,"off")!=0&&latency_kind>=1&&latency_kind<=3) {
            long double workload=0;
            for(size_t i=1;i<c.agents.size();++i) {const auto& a=c.agents[i];workload+=(1+static_cast<long double>(c.close-c.open)/a.interval)*(a.type==2?8*a.depth+6:8);}
            action_latency=std::strcmp(prefetch,"auto")!=0||workload>=32768;
        }
        if(prepare_noise) action_stream=std::make_unique<ActionStream>(c.agents.size(),noise_ids,[this](int agent){return noise_action(agent);},action_latency?std::function<i64()>([this]{return sampled_latency();}):std::function<i64()>());
        else if(action_latency) {action_latency=false;latency_stream=std::make_unique<LatencyStream>([this]{return sampled_latency();});}
    }
    ~Simulation() {stop_latency();}
    void stop_latency() {action_stream.reset();latency_stream.reset();}
    void run() {
        c.wake(0,c.close);for (size_t i=0;i<c.agents.size();++i) c.wake(i,c.now);
        Msg msg;
        while (!c.empty()&&c.now&&c.now<=c.stop) {
#ifdef T3_STANDALONE
            const auto& s=c.top_scheduled();
            if (c.agents[s.recipient()].time>s.time) { c.postpone_top(c.agents[s.recipient()].time);continue; }
            c.pop_top_msg(s,msg);
#elif defined(T3_REF_MESSAGES)
            Msg& msg=c.pop();
#else
            msg=c.pop();
#endif
            auto& a=c.agents[msg.dst];
            a.time=c.now;c.causal=msg.id;
            if (__builtin_expect(msg.kind==WAKE, 0)) {
                msg.recv=c.now;c.delivered(msg);
                if (msg.dst==0) {
                    if (c.now>=c.close) { Msg broadcast=c.make(CLOSE_PRICE);for (int sub:c.subscriptions) send(0,sub,broadcast); }
                } else {
                    if (a.first) { a.first=false;simple(msg.dst,0,CLOSE_REQ); }
                    if (!a.hours) simple(msg.dst,0,HOURS_REQ);
                    if (a.hours&&!a.closed) { c.wake(msg.dst,c.now+a.interval);simple(msg.dst,0,QUERY);a.awaiting=true; }
                }
                a.time+=a.delay;
            } else { a.time+=a.delay;c.delivered(msg);if (msg.dst==0) exchange(msg);else receive(msg); }
            c.recipient_ready(msg.dst,a.time);
        }
    }
};
}
