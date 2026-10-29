#include "../src/core.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<ct::Move> reference(const ct::Board& b) {
    std::vector<ct::Move> out;
    const int dy[] = {-1,1,0,0}, dx[] = {0,0,-1,1};
    for (int p=0;p<int(b.cells.size());++p) {
        if (b.cells[p]) continue;
        std::map<int,std::vector<int>> colors;
        for(int d=0;d<4;++d) {
            int y=p/b.width+dy[d],x=p%b.width+dx[d];
            while(y>=0&&y<b.height&&x>=0&&x<b.width) {
                int q=y*b.width+x;
                if(b.cells[q]) { colors[b.cells[q]].push_back(q); break; }
                y+=dy[d]; x+=dx[d];
            }
        }
        ct::Move m; m.cell=p;
        for(const auto& c:colors) if(c.second.size()>=2) m.removed.insert(m.removed.end(),c.second.begin(),c.second.end());
        std::sort(m.removed.begin(),m.removed.end());
        if(!m.removed.empty()) out.push_back(m);
    }
    return out;
}
int shortest(const ct::Board& start) {
    std::queue<std::pair<std::vector<uint8_t>,int>> queue;
    std::set<std::vector<uint8_t>> seen;
    queue.push({start.cells,0}); seen.insert(start.cells);
    while(!queue.empty()) {
        auto item=queue.front(); queue.pop();
        if(std::none_of(item.first.begin(),item.first.end(),[](uint8_t c){return c!=0;})) return item.second;
        ct::Board b=start; b.cells=item.first;
        for(const auto& m:reference(b)) {
            auto next=b.cells;
            for(int p:m.removed)next[p]=0;
            if(seen.insert(next).second) queue.push({next,item.second+1});
        }
    }
    return -1;
}
void replay(const ct::Board& initial,const ct::SolveResult& result) {
    ct::Board b=initial;
    for(const auto& m:result.moves) {
        auto allowed=reference(b);
        auto it=std::find_if(allowed.begin(),allowed.end(),[&](const auto& a){return a.cell==m.cell;});
        require(it!=allowed.end(),"Returned move is illegal");
        auto removed=m.removed;std::sort(removed.begin(),removed.end());
        require(removed==it->removed,"Returned removal set is wrong");
        for(int p:removed)b.cells[p]=0;
    }
    int remaining=std::count_if(b.cells.begin(),b.cells.end(),[](uint8_t c){return c!=0;});
    require(remaining==result.remaining,"Returned remaining count is wrong");
    if(result.status==ct::SolveStatus::Optimal||result.status==ct::SolveStatus::Feasible)require(!remaining,"Claimed full clear has leftovers");
}
void compare(const ct::Board& b,int table_size) {
    auto ref=reference(b), actual=ct::legal_moves(b);
    auto keys=[](std::vector<ct::Move> moves){std::set<std::pair<int,std::vector<int>>> r;for(auto& m:moves){std::sort(m.removed.begin(),m.removed.end());r.insert({m.cell,m.removed});}return r;};
    require(keys(ref)==keys(actual),"Move generation mismatch");
    const int expected=shortest(b);
    ct::SolveOptions o;o.time_limit_seconds=0;o.threads=1;o.max_table_entries=table_size;
    std::atomic<bool> cancel{false};
    auto result=ct::solve(b,o,cancel);replay(b,result);
    if(expected<0)require(result.status==ct::SolveStatus::Unsolvable,"Unsatisfiable board not proved");
    else {
        require(result.status==ct::SolveStatus::Optimal,"Solvable board not proved optimal");
        require(int(result.moves.size())==expected,"Shortest path length mismatch");
        require(result.lower_bound==expected,"Optimal result bound mismatch");
    }
}
}
int main(int argc,char** argv) {
    try {
        int cases=0;
        ct::Board b;b.width=3;b.height=3;b.cells.assign(9,0);
        for(int mask=0;mask<512;++mask){for(int p=0;p<9;++p)b.cells[p]=(mask>>p)&1;compare(b,64);++cases;}
        std::mt19937 random(20261029);
        for(int n=0;n<2000;++n){for(auto& c:b.cells)c=uint8_t(random()%4);compare(b,(n%2)?64:4096);++cases;}
        b.width=4;b.height=3;b.cells.assign(12,0);
        for(int n=0;n<300;++n){for(auto& c:b.cells)c=uint8_t(random()%4);compare(b,64);++cases;}
        b.width=3;b.height=3;b.cells={0,8,0,0,0,8,0,8,0};compare(b,0);++cases;
        b.cells={0,1,0,2,0,2,0,1,0};compare(b,64);++cases;
        b.cells={0,11,0,0,0,0,0,0,0};require(!ct::validate_board(b),"Invalid color accepted");
        b.width=100;require(!ct::validate_board(b),"Invalid dimensions accepted");
        if(argc>1) {
            std::ifstream f(argv[1]);ct::Board sample;f>>sample.width>>sample.height;sample.cells.resize(sample.width*sample.height);
            for(auto& c:sample.cells){int v;f>>v;c=uint8_t(v);}require(bool(f),"Cannot read sample");
            ct::SolveOptions o;o.threads=4;o.time_limit_seconds=0.05;
            std::atomic<bool> cancel{false};
            auto result=ct::solve(sample,o,cancel);replay(sample,result);
            require(result.seconds<1.0,"Time budget not honored");
            cancel=true;result=ct::solve(sample,o,cancel);replay(sample,result);
            require(result.status==ct::SolveStatus::Cancelled,"Initial cancellation not honored");
            cancel=false;o.time_limit_seconds=0;
            std::thread stopper([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));cancel=true;});
            auto start=std::chrono::steady_clock::now();result=ct::solve(sample,o,cancel);stopper.join();replay(sample,result);
            require(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<1.0,"Cancellation not responsive");
            require(result.status==ct::SolveStatus::Cancelled||result.status==ct::SolveStatus::Optimal,"Cancellation status wrong");
        }
        std::cout<<"PASS: "<<cases<<" boards compared against independent breadth-first shortest-path search.\n";
        std::cout<<"PASS: move generation, solution replay, invalid input, cache limits, time budget and cancellation.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
