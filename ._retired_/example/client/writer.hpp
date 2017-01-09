#ifndef IMAPWRITER_HPP
#define IMAPWRITER_HPP

//#include "../utils.h"
#include <string>
#include <cstring>
#include "system/common.hpp"
#include <list>
#include "openssl/ssl.h"

#define APPSTR(str) str, sizeof(str) - 1
///\cond 0
typedef std::string String;
namespace solid{
class InputStream;
}
class Writer{
public://nonstatic methods
    enum{BUFFLEN = 1024};
    static bool isquotedspecial(uint8 _c);
    Writer(int _sd = -1):sd(_sd), pssl(NULL), wrerr(0), bend(bbeg+BUFFLEN), bpos(bbeg), count(0){}
    ~Writer(){}
    void reinit(int _sd, SSL *_pssl = NULL);
    int flush();
    bool isOk()const{
        return (wrerr >= 0);
    }
    void put(char _c){
        if(bpos != bend){ *(bpos++)=_c; return;}
        flush();
        *(bpos++)=_c;
    }
    void put(char _c1, char _c2){
        if(bpos < (bend -1)) {*(bpos++) = _c1;*(bpos++) = _c2; return;}
        flush();
        *(bpos++) = _c1;*(bpos++) = _c2;
    }
    void put(const char *_s,uint32 _sz);
    void put(uint32 _v);
    void put(const char* _str){
        put(_str,strlen(_str));
    }
    void put(InputStream *_ps, uint64 _len);
    uint32 getWriteCount(bool){
        uint32  rc = count;
        count = 0;
        return rc;
    }
    uint32 getWriteCount()const{
        return count;
    }
    Writer& operator << (char _c){ put(_c); return *this;}
    Writer& operator << (const char* _s){
        put(_s,strlen(_s)); return *this;
    }
    Writer& operator << (const String &_str){
        put(_str.c_str(),_str.size()); return *this;
    }
    Writer& operator << (uint32 _v){
        put(_v); return *this;
    }
    Writer& operator << ( Writer& (*f)(Writer&)){ return f(*this);}
    struct atomstring{
        const char      *str;
        solid::ushort    len;
    };
    Writer& operator<<(atomstring _qs){
        put(_qs.str,_qs.len); return *this;
    }
    struct qstring{
        const char      *str;
        solid::ushort    len;
    };
    Writer& operator<<(qstring _qs){
        put('\"');put(_qs.str,_qs.len);put('\"'); return *this;
    }
    struct lstring{
        const char      *str;
        solid::ushort    len;
    };
    Writer& operator<<(lstring _qs){
        put('{');
        put((uint32) _qs.len);
        put(APPSTR("}\r\n"));
        put(_qs.str,_qs.len);
        return *this;
    }
    struct astring{
       const char       *str;
        solid::ushort    len;
    };
    Writer& operator<<(astring _qs){
        const char *st=_qs.str;
        if(_qs.len > 64){
            //st +=_qs.len;
        }else{
            while(*st!='\0'){
                uint8 c=*st;
                if(isquotedspecial(c)) break;
                ++st;
            }
        }
        if(*st!='\0'){
            //we send a literal
            put('{');
            put((uint32) _qs.len);
            put(APPSTR("}\r\n"));
            put(_qs.str,_qs.len);
            return *this;
        }
        put('\"');put(_qs.str,_qs.len);put('\"');
        return *this;
    }
    struct littp{
        uint32          len;
        InputStream     *pstream;
    };
    Writer& operator<<(littp _t){
        put('{');
        put((uint32) _t.len);
        put(APPSTR("}\r\n"));
        flush();
        if(_t.len == 0) return *this;
        put(_t.pstream, _t.len);
        return *this;
    }
private:
    int             sd;
    SSL             *pssl;
    int             wrerr;
    char            bbeg[BUFFLEN+4];
    char            *bend;
    char            *bpos;
    uint32           count;
};

///\endcond

inline Writer & flush(Writer & _rout){
    _rout.flush(); return _rout;
}

inline Writer & crlf(Writer & _rout){
    _rout.put('\r','\n'); return _rout;
}
///atom
inline Writer::atomstring atom(const char* _str, solid::ushort _len){
    Writer::atomstring qs; qs.str= _str; qs.len = _len;
    return qs;
}
inline Writer::atomstring atom(const String &_str){
    Writer::atomstring qs; qs.str= _str.data(); qs.len = _str.size();
    return qs;
}
///quoted string
inline Writer::qstring qstr(const char* _str){
    Writer::qstring qs; qs.str= _str; qs.len = strlen(_str);
    return qs;
}
inline Writer::qstring qstr(const char* _str, solid::ushort _len){
    Writer::qstring qs; qs.str= _str; qs.len = _len;
    return qs;
}
inline Writer::qstring qstr(const String &_str){
    Writer::qstring qs; qs.str= _str.data(); qs.len = _str.size();
    return qs;
}
///literal string
inline Writer::lstring lstr(const char* _str){
    Writer::lstring qs; qs.str= _str; qs.len = strlen(_str);
    return qs;
}
inline Writer::lstring lstr(const char* _str, solid::ushort _len){
    Writer::lstring qs; qs.str= _str; qs.len = _len;
    return qs;
}
inline Writer::lstring lstr(const String &_str){
    Writer::lstring qs; qs.str= _str.data(); qs.len = _str.size();
    return qs;
}

///astring
inline Writer::astring astr(const char* _str){
    Writer::astring qs; qs.str= _str; qs.len = strlen(_str);
    return qs;
}
inline Writer::astring astr(const char* _str, solid::ushort _len){
    Writer::astring qs; qs.str= _str; qs.len = _len;
    return qs;
}
inline Writer::astring astr(const String &_str){
    Writer::astring qs; qs.str= _str.data(); qs.len = _str.size();
    return qs;
}

inline Writer::littp lit(InputStream* _ps, const uint32 &_len){
    Writer::littp l; l.pstream = _ps; l.len = _len;
    return l;
}

#endif

