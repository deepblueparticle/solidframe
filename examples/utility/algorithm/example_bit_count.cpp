#include <iostream>
#include "solid/utility/common.hpp"

using namespace std;
using namespace solid;

uint32_t compute_bit_count(uint32_t _v){
    uint32_t cnt(0);
    while(_v){
        cnt += (_v & 1);
        _v >>= 1;
    }
    return cnt;
}
int main(int argc, char *argv[]){
    const uint32_t vals[] = {
        1, 2, 3, 4, 5, 6, 7, 1001, 10001, 100001, 1000001, 10000001,
        100000001, 1000000001, 0xf, 0xff, 0xfff, 0xffff, 0xfffff,
        0xffffff, 0xfffffff, 0xffffffff, 0
    };
    if(argc == 2){
        cout<<"Generate table for [0, 255]:"<<endl;
        cout<<'{';
        for(size_t i = 0; i < 256; ++i){
            if(!(i % 16)){
                cout<<endl<<"\t";
            }
            cout<<compute_bit_count(i)<<','<<' ';
        }
        cout<<'}'<<endl;
    }
    const uint32_t *pval(vals);
    do{
        cout<<"bit count for "<<*pval<<" = "<<compute_bit_count(*pval)<<" = "<<bit_count(*pval)<<endl;
    }while(*(++pval));
    return 0;
}
