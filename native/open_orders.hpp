#pragma once
namespace t3 {
// Order IDs are normally appended in creation order. Erasure preserves that
// order, so iteration matches std::map without one allocation per live order.
class FlatOpenOrders {
    std::vector<std::pair<i64,Order>> items;
public:
    using iterator=decltype(items)::iterator;
    using const_iterator=decltype(items)::const_iterator;
    iterator begin() {return items.begin();} iterator end() {return items.end();}
    const_iterator begin() const {return items.begin();} const_iterator end() const {return items.end();}
    size_t size() const {return items.size();}
    iterator find(i64 id) {
        auto it=std::lower_bound(items.begin(),items.end(),id,[](const auto& item,i64 key){return item.first<key;});
        return it!=items.end()&&it->first==id?it:items.end();
    }
    void emplace(i64 id,const Order& order) {
        if(items.empty()||items.back().first<id) {items.emplace_back(id,order);return;}
        auto it=std::lower_bound(items.begin(),items.end(),id,[](const auto& item,i64 key){return item.first<key;});
        if(it==items.end()||it->first!=id) items.emplace(it,id,order);
    }
    void erase(iterator it) {items.erase(it);}
    void erase(i64 id) {auto it=find(id);if(it!=end()) erase(it);}
};
}
