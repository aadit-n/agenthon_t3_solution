#pragma once
#include <deque>

namespace t3 {
// Stable pooled levels, with a compact ordered price index for ordinary books.
// Large price sets switch to a tree; order nodes and FIFO links stay unchanged.
class FlatPriceBook {
    static constexpr uint32_t NONE=UINT32_MAX;
    static constexpr size_t TREE_THRESHOLD=1024;
    struct Level {uint32_t head=NONE,tail=NONE;i64 quantity=0,price=0;};
    struct Entry {i64 price;Level* level;};
    struct Node {Order order;uint32_t prev=NONE,next=NONE;Level* level=nullptr;};
    struct Side {
#ifdef T3_DEQUE_PRICES
        std::deque<Entry> index;
#else
        std::vector<Entry> index;
#endif
        std::map<i64,Level*> tree;
        bool large=false;
    };
    struct Cached {i64 price=0;Level* level=nullptr;};
    Side sides[2];
    Cached cache[2][256];
    std::deque<Level> pool;
    std::vector<Level*> free_levels;
    std::vector<Node> nodes;
    std::vector<uint32_t> by_id,free_nodes;
    Level* best[2]={nullptr,nullptr};
    i64 prices[2]={0,0};
    auto position(int side,i64 price) {
        auto& index=sides[side].index;
        return std::lower_bound(index.begin(),index.end(),price,[side](const Entry& entry,i64 value) {
            return side==0?entry.price<value:entry.price>value;
        });
    }
    void refresh(int side) {
        auto& s=sides[side];
        if(s.large) best[side]=s.tree.empty()?nullptr:(side==0?std::prev(s.tree.end())->second:s.tree.begin()->second);
        else best[side]=s.index.empty()?nullptr:s.index.back().level;
        prices[side]=best[side]?best[side]->price:0;
    }
    Level* allocate(i64 price) {
        Level* level;
        if(free_levels.empty()) {pool.emplace_back();level=&pool.back();}
        else {level=free_levels.back();free_levels.pop_back();}
        *level=Level{};level->price=price;return level;
    }
    Level* level_for(int side,i64 price) {
        auto& hit=cache[side][uint64_t(price)&255];
        if(hit.level&&hit.price==price) return hit.level;
        auto& s=sides[side];Level* level;
        if(s.large) {
            auto it=s.tree.lower_bound(price);
            if(it!=s.tree.end()&&it->first==price) level=it->second;
            else {level=allocate(price);s.tree.emplace_hint(it,price,level);}
        } else {
#ifdef T3_DEQUE_PRICES
            if(s.index.empty()) {level=allocate(price);s.index.push_back({price,level});}
            else if(price==s.index.front().price) level=s.index.front().level;
            else if(price==s.index.back().price) level=s.index.back().level;
            else if(side==0?price<s.index.front().price:price>s.index.front().price) {level=allocate(price);s.index.push_front({price,level});}
            else if(side==0?price>s.index.back().price:price<s.index.back().price) {level=allocate(price);s.index.push_back({price,level});}
            else {
                auto it=position(side,price);
                if(it!=s.index.end()&&it->price==price) level=it->level;
                else {level=allocate(price);s.index.insert(it,Entry{price,level});}
            }
            if(s.index.size()>TREE_THRESHOLD) {
                for(const auto& entry:s.index) s.tree.emplace(entry.price,entry.level);
                s.index.clear();s.large=true;
            }
#else
            auto it=position(side,price);
            if(it!=s.index.end()&&it->price==price) level=it->level;
            else {
                level=allocate(price);s.index.insert(it,Entry{price,level});
                if(s.index.size()>TREE_THRESHOLD) {
                    for(const auto& entry:s.index) s.tree.emplace(entry.price,entry.level);
                    s.index.clear();s.large=true;
                }
            }
#endif
        }
        hit={price,level};return level;
    }
    void remove(uint32_t slot) {
        auto& node=nodes[slot];auto* level=node.level;int side=node.order.side;
        if(node.prev!=NONE) nodes[node.prev].next=node.next;else level->head=node.next;
        if(node.next!=NONE) nodes[node.next].prev=node.prev;else level->tail=node.prev;
        by_id[size_t(node.order.id)]=NONE;node.level=nullptr;free_nodes.push_back(slot);
        if(level->head!=NONE) return;
        auto& hit=cache[side][uint64_t(level->price)&255];if(hit.level==level) hit.level=nullptr;
        auto& s=sides[side];bool was_best=best[side]==level;
        if(s.large) s.tree.erase(level->price);
        else if(was_best) s.index.pop_back();
        else {
            auto it=position(side,level->price);
            if(it==s.index.end()||it->level!=level) throw std::logic_error("missing price level");
            s.index.erase(it);
        }
        free_levels.push_back(level);if(was_best) refresh(side);
    }
public:
    FlatPriceBook() {
        nodes.reserve(1024);by_id.reserve(4096);free_nodes.reserve(1024);
#ifndef T3_DEQUE_PRICES
        sides[0].index.reserve(64);sides[1].index.reserve(64);
#endif
    }
    i64 best_price(int side) const {return prices[side];}
    i64 best_size(int side) const {return best[side]?best[side]->quantity:0;}
    bool cross(const Order& order) const {
        return best[1-order.side]&&(order.side==0?order.price>=prices[1]:order.price<=prices[0]);
    }
    Order front(int side) const {return nodes[best[1-side]->head].order;}
    void enter(const Order& order) {
        if(order.id<0) throw std::invalid_argument("negative resting order ID");
        if(size_t(order.id)>=by_id.size()) by_id.resize(size_t(order.id)+1,NONE);
        if(by_id[size_t(order.id)]!=NONE) throw std::invalid_argument("duplicate resting order ID");
        uint32_t slot;
        if(free_nodes.empty()) {slot=nodes.size();nodes.emplace_back();}
        else {slot=free_nodes.back();free_nodes.pop_back();}
        auto* level=level_for(order.side,order.price);auto& node=nodes[slot];
        node={order,level->tail,NONE,level};
        if(level->tail!=NONE) nodes[level->tail].next=slot;else level->head=slot;
        level->tail=slot;level->quantity+=order.quantity;by_id[size_t(order.id)]=slot;
        if(!best[order.side]||(order.side==0?order.price>prices[0]:order.price<prices[1])) {best[order.side]=level;prices[order.side]=order.price;}
    }
    Order take(int side,i64 quantity) {
        uint32_t slot=best[1-side]->head;auto& node=nodes[slot];Order result=node.order;
        result.quantity=std::min(quantity,result.quantity);result.fill_price=result.price;
        node.order.quantity-=result.quantity;node.level->quantity-=result.quantity;
        if(!node.order.quantity) remove(slot);return result;
    }
    Order cancel(const Order& request) {
        if(request.id<0||size_t(request.id)>=by_id.size()) return Order{};
        uint32_t slot=by_id[size_t(request.id)];if(slot==NONE) return Order{};
        auto& node=nodes[slot];if(node.order.side!=request.side||node.order.price!=request.price) return Order{};
        Order result=node.order;node.level->quantity-=result.quantity;remove(slot);return result;
    }
};
}
