#pragma once
#include <stdexcept>

namespace t3 {
// Dense order IDs index recycled contiguous slots; FIFO links are slot indices.
// Price levels remain ordered maps, supporting arbitrary int64 prices without
// an artificial price window. Live allocations are per level, never per order.
class PooledBook {
    static constexpr uint32_t NONE=UINT32_MAX;
    struct PriceLevel {uint32_t head=NONE,tail=NONE;i64 quantity=0;std::map<i64,PriceLevel>::iterator it;};
    struct Node {Order order;uint32_t prev=NONE,next=NONE;PriceLevel* level=nullptr;};
    std::map<i64,PriceLevel> levels[2];
    std::vector<Node> nodes;
    std::vector<uint32_t> by_id,free;
    PriceLevel* best[2]={nullptr,nullptr};
    i64 price[2]={0,0};
#ifdef T3_PRICE_CACHE
    struct Cached {i64 price=0;PriceLevel* level=nullptr;};
    Cached cache[2][4096];
    PriceLevel& level_for(int side,i64 value) {
        auto& hit=cache[side][uint64_t(value)&4095];
        if(hit.level&&hit.price==value) return *hit.level;
        auto [pos,inserted]=levels[side].try_emplace(value);
        if(inserted) pos->second.it=pos;
        hit={value,&pos->second};return pos->second;
    }
#endif
    void refresh(int side) {
        if(levels[side].empty()) {best[side]=nullptr;price[side]=0;return;}
        auto it=side==0?std::prev(levels[side].end()):levels[side].begin();
        best[side]=&it->second;price[side]=it->first;
    }
    void remove(uint32_t slot) {
        auto& node=nodes[slot];auto* level=node.level;int side=node.order.side;
        if(node.prev!=NONE) nodes[node.prev].next=node.next;else level->head=node.next;
        if(node.next!=NONE) nodes[node.next].prev=node.prev;else level->tail=node.prev;
        by_id[size_t(node.order.id)]=NONE;node.level=nullptr;free.push_back(slot);
        if(level->head==NONE) {
#ifdef T3_PRICE_CACHE
            auto& hit=cache[side][uint64_t(node.order.price)&4095];
            if(hit.level==level) hit.level=nullptr;
            bool was_best=(level==best[side]);
            if(was_best) {
                if(side==0) {
                    if(level->it==levels[0].begin()) {best[0]=nullptr;price[0]=0;}
                    else {auto prev_it=std::prev(level->it);best[0]=&prev_it->second;price[0]=prev_it->first;}
                } else {
                    auto next_it=std::next(level->it);
                    if(next_it==levels[1].end()) {best[1]=nullptr;price[1]=0;}
                    else {best[1]=&next_it->second;price[1]=next_it->first;}
                }
            }
            levels[side].erase(level->it);
#else
            levels[side].erase(node.order.price);refresh(side);
#endif
        }
    }
public:
    PooledBook() {nodes.reserve(8192);by_id.resize(131072,NONE);free.reserve(8192);}
    i64 best_price(int side) const {return price[side];}
    i64 best_size(int side) const {return best[side]?best[side]->quantity:0;}
    bool cross(const Order& order) const {
        return best[1-order.side]&&(order.side==0?order.price>=price[1]:order.price<=price[0]);
    }
    Order front(int side) const {return nodes[best[1-side]->head].order;}
    void enter(const Order& order) {
        if(__builtin_expect(order.id<0, 0)) throw std::invalid_argument("negative resting order ID");
        if(__builtin_expect(size_t(order.id)>=by_id.size(), 0)) by_id.resize(std::max(size_t(order.id)*2, size_t(131072)),NONE);
        if(__builtin_expect(by_id[size_t(order.id)]!=NONE, 0)) throw std::invalid_argument("duplicate resting order ID");
        uint32_t slot;
        if(free.empty()) {slot=nodes.size();nodes.emplace_back();}
        else {slot=free.back();free.pop_back();}
#ifdef T3_PRICE_CACHE
        auto& level=level_for(order.side,order.price);
#else
        auto [pos,inserted]=levels[order.side].try_emplace(order.price);
        if(inserted) pos->second.it=pos;
        auto& level=pos->second;
#endif
        auto& node=nodes[slot];
        node={order,level.tail,NONE,&level};
        if(level.tail!=NONE) nodes[level.tail].next=slot;else level.head=slot;
        level.tail=slot;level.quantity+=order.quantity;by_id[size_t(order.id)]=slot;
        if(!best[order.side]||(order.side==0?order.price>price[0]:order.price<price[1])) {
            best[order.side]=&level;price[order.side]=order.price;
        }
    }
    Order take(int incoming_side,i64 quantity) {
        uint32_t slot=best[1-incoming_side]->head;auto& node=nodes[slot];Order result=node.order;
        result.quantity=std::min(quantity,result.quantity);result.fill_price=result.price;
        node.order.quantity-=result.quantity;node.level->quantity-=result.quantity;
        if(!node.order.quantity) remove(slot);return result;
    }
    Order cancel(const Order& request) {
        if(__builtin_expect(request.id<0||size_t(request.id)>=by_id.size(), 0)) return Order{};
        uint32_t slot=by_id[size_t(request.id)];if(__builtin_expect(slot==NONE, 0)) return Order{};
        auto& node=nodes[slot];if(__builtin_expect(node.order.side!=request.side||node.order.price!=request.price, 0)) return Order{};
        Order result=node.order;node.level->quantity-=result.quantity;remove(slot);return result;
    }
};
}
