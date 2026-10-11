#pragma once
namespace t3 {
// A busy recipient postpones every arrived message to its next free time.
// Coalesce those scheduling operations, retaining exact secondary tie keys.
class RecipientQueue {
    struct Tied {
        bool operator()(const CompactScheduled& a,const CompactScheduled& b) const {
            return std::make_pair(a.routing(),a.id)>std::make_pair(b.routing(),b.id);
        }
    };
    struct Recipient {
        i64 ready=INT64_MIN;
        std::priority_queue<CompactScheduled> future;
        std::priority_queue<CompactScheduled,std::vector<CompactScheduled>,Tied> arrived;
    };
    std::vector<Recipient> recipients;
    std::vector<CompactScheduled> heads;
    std::vector<size_t> positions;
    size_t count=0;
    bool tail=false;
    std::priority_queue<CompactScheduled> fallback;
    static bool earlier(const CompactScheduled& a,const CompactScheduled& b) {return b<a;}
    void swap_heads(size_t a,size_t b) {std::swap(heads[a],heads[b]);positions[heads[a].recipient()]=a;positions[heads[b].recipient()]=b;}
    void repair(size_t pos) {
        if(pos&&earlier(heads[pos],heads[(pos-1)/2])) {
            while(pos&&earlier(heads[pos],heads[(pos-1)/2])) {size_t parent=(pos-1)/2;swap_heads(pos,parent);pos=parent;}
        } else {
            while(pos*2+1<heads.size()) {
                size_t child=pos*2+1;if(child+1<heads.size()&&earlier(heads[child+1],heads[child])) ++child;
                if(!earlier(heads[child],heads[pos])) break;swap_heads(pos,child);pos=child;
            }
        }
    }
    void refresh(int dst) {
        auto& r=recipients[dst];
        while(!r.future.empty()&&r.future.top().time<=r.ready) {r.arrived.push(r.future.top());r.future.pop();}
        size_t pos=positions[dst];
        if(r.arrived.empty()&&r.future.empty()) {
            if(pos==SIZE_MAX) return;
            positions[dst]=SIZE_MAX;
            if(pos+1==heads.size()) {heads.pop_back();return;}
            heads[pos]=heads.back();heads.pop_back();positions[heads[pos].recipient()]=pos;repair(pos);return;
        }
        CompactScheduled next;
        if(!r.arrived.empty()) {next=r.arrived.top();next.time=r.ready;} else next=r.future.top();
        if(pos==SIZE_MAX) {pos=heads.size();positions[dst]=pos;heads.push_back(next);} else heads[pos]=next;
        repair(pos);
    }
    void ensure(int dst) {
        if(size_t(dst)>=recipients.size()) {recipients.resize(size_t(dst)+1);positions.resize(size_t(dst)+1,SIZE_MAX);}
    }
public:
    bool empty() const {return count==0;}
    void ready(int dst,i64 time) {if(tail) return;ensure(dst);recipients[dst].ready=time;refresh(dst);}
    void push(const CompactScheduled& event) {
        ++count;if(tail) {fallback.push(event);return;}int dst=event.recipient();ensure(dst);auto& r=recipients[dst];
        if(event.time<=r.ready) r.arrived.push(event);else r.future.push(event);refresh(dst);
    }
    const CompactScheduled& top() const {return tail?fallback.top():heads.front();}
    // Preserve the reference loop's single pop beyond stop, including the case
    // where that pop only postpones a busy message and emits no ledger row.
    void before_pop(i64 stop) {
        if(tail||heads.front().time<=stop) return;
        for(auto& r:recipients) {
            auto retain=[&](CompactScheduled event) {if(event.time<=stop) event.time=std::max(event.time,r.ready);fallback.push(event);};
            while(!r.future.empty()) {retain(r.future.top());r.future.pop();}
            while(!r.arrived.empty()) {retain(r.arrived.top());r.arrived.pop();}
        }
        heads.clear();tail=true;
    }
    void pop() {
        --count;if(tail) {fallback.pop();return;}
        int dst=heads.front().recipient();auto& r=recipients[dst];
        if(!r.arrived.empty()) r.arrived.pop();else r.future.pop();refresh(dst);
    }
};
}
