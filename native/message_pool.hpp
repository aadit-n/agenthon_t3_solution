#pragma once
#include <array>
#include <memory>
namespace t3 {
// Chunk addresses remain stable when new messages are scheduled by a callback.
class MessagePool {
    std::vector<std::unique_ptr<std::array<Msg,128>>> blocks;
    size_t count=0;
public:
    void reserve(size_t n) {blocks.reserve((n+127)/128);}
    size_t size() const {return count;}
    Msg& operator[](size_t i) {return (*blocks[i/128])[i%128];}
    void push_back(const Msg& msg) {
        if(count%128==0) blocks.push_back(std::make_unique<std::array<Msg,128>>());
        (*this)[count++]=msg;
    }
};
}
