#define main participant_main
#include "main_lite.cpp"
#undef main
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    fs::path output=argc>1?argv[1]:"/output/row-reuse-tests";fs::create_directories(output);
    size_t ledger_rows=0,trace_rows=0;
    {
        LedgerStream stream(output/"stream-ledger.parquet");
        litepq::Writer reference(output/"reference-ledger.parquet",litepq::ledger_schema());
        std::vector<LedgerRow> rows;
        for(int block=0;block<40;++block) {
            size_t count=block%3==0?65536:block%3==1?4096:128;
            for(size_t i=0;i<count;++i) {
                i64 id=i64(ledger_rows+i);
                rows.push_back({id+1,id*9,id*9+5,5,id? id-1:-1,id%127,int(id%13),int(id%7),int(id%13)});
            }
            litepq::ledger_block(reference,rows,i64(ledger_rows));ledger_rows+=count;
            rows=stream.submit(std::move(rows));assert(rows.empty());
        }
        auto expected=reference.close();assert(stream.finish().first==expected);
    }
    {
        TraceStream stream(output/"stream-trace.parquet");
        litepq::Writer reference(output/"reference-trace.parquet",litepq::trace_schema());
        std::vector<TraceRow> rows;
        for(int block=0;block<40;++block) {
            size_t count=block%2?4096:16384;
            for(size_t i=0;i<count;++i) {
                i64 id=i64(trace_rows+i);
                rows.push_back({id/4,100000+id%7,1+id%3,id%127,id,int(id%13),id%3?SUBMITTED:EXEC,int(id%2)});
            }
            auto expected=rows;
            std::sort(expected.begin(),expected.end(),[](const TraceRow& a,const TraceRow& b){return std::tie(a.time,a.order_id,a.agent,a.rank)<std::tie(b.time,b.order_id,b.agent,b.rank);});
            reference.append_trace(expected,0,expected.size());trace_rows+=count;
            rows=stream.submit(std::move(rows));assert(rows.empty());
        }
        auto expected=reference.close();assert(stream.finish().first==expected);
    }
    {
        LedgerStream stream(output/"empty.parquet");std::vector<LedgerRow> empty;
        assert(stream.submit(std::move(empty)).empty());stream.finish();
    }
    {
        bool failed=false;
        try {LedgerStream stream(output/"missing-directory"/"failure.parquet");
            std::vector<LedgerRow> rows(1);stream.submit(std::move(rows));stream.finish();
        } catch(const std::exception&) {failed=true;}
        assert(failed);
    }
    std::cout<<ledger_rows<<" ledger rows and "<<trace_rows<<" trace rows: bounded buffer reuse, backpressure, sorting, deferred fill labels, empty finish and writer failure passed\n";
}
