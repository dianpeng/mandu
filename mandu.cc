#include "mandu.h"
#include <list>
#include <map>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>

#define UNREACHABLE(x) \
   do { \
       assert(0&&"Unreachable"); \
       x; \
   } while(0)

#define UNUSED_VARIABLE(x) \
    do { \
        (void)(x); \
    } while(0)

namespace {
using mandu::Mandu;
using mandu::SoupMaker;

char* string_to_array( std::string* str , int pos ) {
    return &(*str->begin()) + pos;
}

const char* string_to_array( const std::string* str , int pos ) {
    return &(*str->begin()) + pos;
}

enum TokenId {
    TK_SECTION_START, TK_SECTION_END ,
    TK_LSQR,TK_RSQR,TK_LBRA,TK_RBRA,
    TK_NUMBER,TK_STRING,TK_VARIABLE,
    TK_COMMA,TK_SUB,TK_DOLLAR,TK_UNKNOWN,
    TK_END, TK_EOF
};

struct Lexme {
    TokenId token;
    std::size_t length;

    Lexme():
        token( TK_UNKNOWN ),
        length(-1)
    {}

    Lexme( TokenId tk , std::size_t len ) :
        token(tk),
        length(len)
    {}
};


bool IsInitialVariableChar( int cha )  {
    return cha == '_' || std::isalpha(cha);
}

bool IsRestVariableChar( int cha ) {
    return IsInitialVariableChar(cha) || std::isdigit(cha);
}

bool IsExecutorBodyEscapeChar( int cha ) {
    switch( cha ) {
        case 't':
        case '$':
            return true;
        default:
            return false;
    }
}

bool IsExecutorStringLiteralEscapeChar( int cha ) {
    switch(cha) {
        case '\\':
        case '\"':
            return true;
        default:
            return false;
    }
}

const char* GetTokenName( int token_id );

class Tokenizer {
public:
    Tokenizer( const std::string& source , int position ) :
        source_(&source),
        cur_lexme_(),
        position_( position )
    {}

    Tokenizer():
        source_(NULL),
        cur_lexme_(),
        position_(-1)
    {}

    void Bind( const std::string& source , int position ) {
        source_ = &source;
        Set(position);
    }

    void GetLocation( int* line, int* ccount );

    Lexme Next() {
        return (cur_lexme_ = Peek());
    }

    Lexme Peek() const;

    Lexme cur_lexme() const {
        return cur_lexme_;
    }

    int position() const {
        return position_;
    }

    const std::string& source() const {
        return *source_;
    }

    void Move( int offset ) {
        position_ += offset;
        Next();
    }

    void Move() {
        position_ += cur_lexme_.length;
        Next();
    }

    void Set( int pos ) {
        position_ = pos;
        Next();
    }

private:

    int NextChar( int pos ) const {
        return source_->size() <= static_cast<std::size_t>(pos) ? 0 : source_->at(pos);
    }

    void SkipWhitespace () const ;

private:

    // Source pointer pointed to the source file binded to this Tokenizer
    const std::string* source_;
    // Current lexme for this tokenizer
    Lexme cur_lexme_;
    // Position of the current tokenizer
    mutable std::size_t position_;
};

void Tokenizer::GetLocation( int* line , int* ccount ) {
    *line = *ccount = 0;
    for( std::size_t i = 0 ; i < position_ ; ++i ) {
        if( source_->at(i) == '\n' ) {
            ++(*line);
            *ccount = 0;
        } else {
            ++(*ccount);
        }
    }
}

void Tokenizer::SkipWhitespace() const {
    int i;

    for( i = position_ ; std::isspace( source_->at(i) ) ; ++i )
        ;
    position_ = static_cast<std::size_t>(i);
}

Lexme Tokenizer::Peek() const {
    do {
        int cha = NextChar( position_ );
        switch(cha) {
            case 0:
                return Lexme(TK_EOF,1);
            case '`':
                return Lexme(TK_END,1);
            case ' ':case '\t':case '\v':
            case '\n':case '\r':
                SkipWhitespace();
                continue;
            case '[' :
                return Lexme(TK_LSQR,1);
            case ']':
                return Lexme(TK_RSQR,1);
            case '{':
                return Lexme(TK_LBRA,1);
            case '}':
                return Lexme(TK_RBRA,1);
            case '<':
                return Lexme(TK_SECTION_START,1);
            case '>':
                return Lexme(TK_SECTION_END,1);
            case '-':
                return Lexme(TK_SUB,1);
            case ',':
                return Lexme(TK_COMMA,1);
            case '0':case '1':case '2':case '3':case '4':
            case '5':case '6':case '7':case '8':case '9':
                return Lexme(TK_NUMBER,0);
            case '\"':
                return Lexme(TK_STRING,0);
            default:
                if( IsInitialVariableChar(cha) ) {
                    return Lexme(TK_VARIABLE,0);
                } else {
                    return Lexme();
                }
        }
    } while(true);
}


// Section skipper is crafted for skipping the section body once the section
// key is not shown. The core here is that it needs to skip the string literal
// variable and other stuff. This require us do a simple parse for the script
class SectionSkipper {
public:
    SectionSkipper( const std::string& source , int position ):
        tokenizer_(source,position)
        {}
    // Skip the section body now.
    int Skip( std::string* error );
private:

    void ReportError( std::string* error , const char* format,  ... );

    bool SkipString( std::string* error );
    void SkipNumber( );
    void SkipVariable( );

    bool IsStringLiteralEscapeChar( int position ) {
        if( position < static_cast<int>( tokenizer_.source().size() ) ) {
            return IsExecutorStringLiteralEscapeChar(
                    tokenizer_.source().at(position) );
        } else {
            return false;
        }
    }

    bool IsBodyEscapeChar( int position ) {
        if( position < static_cast<int>( tokenizer_.source().size() ) ) {
            return IsExecutorStringLiteralEscapeChar(
                    tokenizer_.source().at(position));
        } else {
            return false;
        }
    }

private:
    Tokenizer tokenizer_;
};

void SectionSkipper::ReportError( std::string* error, const char* format , ... ) {
    int line,ccount;
    char msg[1024];
    va_list vl;
    std::stringstream formatter;

    va_start(vl,format);
    tokenizer_.GetLocation(&line,&ccount);

    vsprintf(msg,format,vl);
    formatter<<"[Error("<<line<<","<<ccount<<"]:"
        <<msg<<std::endl;
    error->assign( formatter.str() );
}

bool SectionSkipper::SkipString( std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_STRING );
    std::size_t i ;

    for( i = tokenizer_.position()+1 ;
            i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);
        if( cha == '\\' ) {
            if( IsStringLiteralEscapeChar( i+1 ) ) {
                ++i;
            }
        } else {
            if( cha == '"' )
                break;
        }
    }

    if( i == tokenizer_.source().size() ) {
        ReportError(error,"Unexpected end of the stream in string literal!");
        return false;
    }

    tokenizer_.Set(i+1);
    return true;
}

void SectionSkipper::SkipNumber( ) {
    // We don't have sign for number, lukcy !
    std::size_t i ;
    for( i = tokenizer_.position() ;
            i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);
        if( !std::isdigit(cha) )
            break;
    }
    tokenizer_.Set(i);
}

void SectionSkipper::SkipVariable( ) {
    std::size_t i ;
    for( i = tokenizer_.position()+1;
            i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);
        if( !IsRestVariableChar(cha) )
            break;
    }
    tokenizer_.Set(i);
}

int SectionSkipper::Skip( std::string* error ) {
    do {
        Lexme l = tokenizer_.Next();
        switch(l.token) {
            case TK_STRING:
                if(!SkipString(error))
                    return -1;
                break;
            case TK_VARIABLE:
                SkipVariable();
                break;
            case TK_NUMBER:
                SkipNumber();
                break;
            case TK_SECTION_END:
                // SectionEnd
                tokenizer_.Move();
                return tokenizer_.position();
            case TK_END:
            case TK_EOF:
            case TK_UNKNOWN:
                // This token cannot be here at the section body
                ReportError(error,"Unexpected token or end of the file!");
                return -1;
            default:
                tokenizer_.Move();
                break;
        }
    } while(true);
}
}// namespace

namespace mandu {
namespace detail {

template< typename T >
class ZoneAllocator {
public:
    ZoneAllocator( std::size_t cap , std::size_t max_capacity ):
        cur_capacity_( cap < 2 ? 1 : cap/2 ),
        max_capacity_( max_capacity ),
        page_list_( NULL ),
        free_list_( NULL )
        {}

    ~ZoneAllocator() {
        Clear(0);
    }

    // Grab function performs memory allocation and construction function for
    // each memory segment. currently 3 maximum parameters are allowed. C++11
    // can use variadic template to solve it :(
    T* Grab() {
        return ::new (Malloc()) T();
    }

    template< typename A1 >
    T* Grab( const A1& a1 ) {
        return ::new (Malloc()) T(a1);
    }

    template< typename A1 , typename A2 >
    T* Grab( const A1& a1 , const A2& a2 ) {
        return ::new (Malloc()) T(a1,a2);
    }

    template< typename A1 , typename A2 , typename A3 >
    T* Grab( const A1& a1 , const A2& a2 , const A3& a3 ) {
        return ::new (Malloc()) T(a1,a2,a3);
    }

    // Drop function for that objects.
    void Drop( T* ptr ) {
        ptr->~T();
        Free(ptr);
    }

    // Clear function will Clear the memory, as its name indicated, free all
    // the page. To hold those memory inside of the memory, please use Reclaim
    void Clear( std::size_t cap );

    // Reclaim will reclaim all the
    void Reclaim();

private:
    void* Malloc() {
        void* ret = free_list_;
        if( ret == NULL ) {
            Grow();
        }
        assert( free_list_ != NULL );
        ret = free_list_ ;
        free_list_ = free_list_->next;
        return ret;
    }

    void Free( void* ptr ) {
        FreeList* fl = static_cast<FreeList*>(ptr);
        fl->next = free_list_;
        free_list_ = fl;
    }

    void Grow( void* , std::size_t );

    void Grow() {
        cur_capacity_ *= 2;
        cur_capacity_ = std::min( cur_capacity_ , max_capacity_ );
        void* page = malloc( cur_capacity_*Align(sizeof(T),kAlignment) + sizeof(Page) );
        Page* p = reinterpret_cast<Page*>(page);
        p->page_size = cur_capacity_;
        p->next = page_list_;
        page_list_ = p;
        Grow( static_cast<void*>(
                    static_cast<char*>(page) + sizeof(Page)),cur_capacity_ );
    }

    std::size_t Align( std::size_t l , std::size_t a ) {
        return (l+a-1) &~(a-1);
    }

private:

    // Default alignment for the ZoneAllocator. It must be larger than the
    // sizeof(void*) which enable us to store that pointer there
    static const std::size_t kAlignment = sizeof(void*);

    std::size_t cur_capacity_;
    std::size_t max_capacity_;

    // FreeList structure is a list that stores all the available empty
    // free memory chunk of sizeof(T).
    struct FreeList {
        FreeList* next;
    };

    // Page structure is a list that holds all the allocated page( per page
    // per malloc operations). The minimum intenral fragmentation size is
    // sizeof(Page).
    struct Page {
        std::size_t page_size;
        struct Page* next;
    };

    Page* page_list_;

    FreeList* free_list_;
};

template< typename T >
void ZoneAllocator<T>::Grow( void* mem , std::size_t sz ) {
    void* head = mem;
    FreeList* fl;
    fl = reinterpret_cast<FreeList*>(mem);
    for( std::size_t i = 0 ; i < sz - 1 ; ++i ) {
        fl->next = reinterpret_cast<FreeList*>(
            reinterpret_cast<char*>(fl) + Align(sizeof(T),kAlignment));
        fl = fl->next;
    }
    fl->next = free_list_;
    free_list_ = static_cast<FreeList*>(head);
}

template< typename T >
void ZoneAllocator<T>::Clear( std::size_t sz ) {
    Page* p = page_list_;
    while(p) {
        page_list_ = p->next;
        free(p);
        p = page_list_;
    }
    page_list_ = NULL;
    free_list_ = NULL;
    cur_capacity_ = sz/2;
}

template< typename T >
void ZoneAllocator<T>::Reclaim() {
    page_list_ = NULL;
    Page* p = page_list_;
    while(p) {
        Grow( static_cast<char*>(p) + sizeof(Page) , p->page_size );
        p = p->next;
    }
}

class Executor {
public:
    static const std::size_t kMemoryPoolInitialSize = 64;
    static const std::size_t kMemoryPoolMaximumSize = 512;

    Executor():
        mandu_pool_( kMemoryPoolInitialSize , kMemoryPoolMaximumSize )
        {}

    ~Executor() {
        Clear();
    }

    // Delegate function
    bool IsSectionEnabled( const std::string& section_key ) const;
    bool EnableSection( const std::string& section_key );
    bool DisableSection( const std::string& section_key );

    Mandu* NewMandu( const std::string& section_key , const std::string& key );
    Mandu* NewMandu( const std::string& key );

    void FreeMandu( Mandu* mandu ) {
        mandu_pool_.Drop(mandu);
    }

    void Clear();

    bool Cook( const std::string& text , std::string* output , std::string* error );
private:
    void ReportError( std::string* error , const char* format , ... );

    bool ParseString( Mandu* output , std::string* error );
    bool ParseNumber( Mandu* number , std::string* error );
    bool ParseVariable( const std::string& section , Mandu* var , std::string* error );
    bool ParseAtomic( const std::string& section , Mandu* val , std::string* error );

    enum {
        ELEMENT_RANGE,
        ELEMENT_ATOMIC,
        ELEMENT_LIST,
        ELEMENT_FAIL
    };

    // This function will parse the list element in different format.
    // The return value can be used to decide which type of list elements has been
    // processed. Additionally, for element that is a list, the outputs array will
    // be pushed on top of it.
    int ParseListElement( const std::string& section , Mandu* from ,
            Mandu* to , std::list<Mandu*>* outputs , std::string* error );

    bool ParseList( const std::string& section, std::list<Mandu*>* outputs, std::string* error );

    bool ExecuteList( const std::string& section , std::list<std::string>* output , std::string* error );
    bool ExecuteAtomic( const std::string& section , std::list<std::string>* output , std::string* error );
    bool ExecuteBody( const Mandu& dollar_value , std::string* output , std::string* error );
    bool Execute( std::list<std::string>* output , std::string* error );

    // Executes the template with the output for a list , at very last
    // we will start to do concatenation
    bool DoExecute( std::string* output , std::string* error );

    bool LookUpVariable( const std::string& section_name , const std::string& variable_name , Mandu* mandu ) const;

    void Concatenate( const std::list<std::string>& input, std::string* output );

private:
    bool IsBodyEscapeChar( int position ) {
        if( position < static_cast<int>(
                    tokenizer_.source().size()) ) {
            return IsExecutorBodyEscapeChar( tokenizer_.source().at(position) );
        } else {
            return false;
        }
    }

    bool IsStringLiteralEscapeChar( int position ) {
        if( position < static_cast<int>(
                    tokenizer_.source().size()) ) {
            return IsExecutorStringLiteralEscapeChar( tokenizer_.source().at(position) );
        } else {
            return false;
        }
    }

private:
    // Internally manage all the mandu memory allocation
    ZoneAllocator<Mandu> mandu_pool_;

    typedef std::map<std::string,Mandu*> ManduMap;
    struct ManduSection {
        bool enabled;
        ManduMap mandu_map;
        ManduSection():
            enabled(true)
        {}
    };

    typedef std::map<std::string,ManduSection> SectionMap;

    // Global map stores variable that is default to global scope .If user doesn't
    // specify any section options, then all the variable will be treated inside of
    // the global variable scope.
    ManduMap global_var_;

    // SectionMap enable user to output text based on the section. If section is on,
    // then the variable that is in that section will be allowed to be used , and the
    // substitution commands for that part will be utilized as well. Otherwise the
    // corresponding section will be skiped silently.
    SectionMap section_var_;


    // Tokenizer for this executor
    Tokenizer tokenizer_;
};


void Executor::ReportError( std::string* error, const char* format , ... ) {
    int line,ccount;
    char msg[1024];
    va_list vl;
    std::stringstream formatter;

    va_start(vl,format);
    tokenizer_.GetLocation(&line,&ccount);

    vsprintf(msg,format,vl);
    formatter<<"[Error("<<line<<","<<ccount<<"]:"
        <<msg<<std::endl;
    error->assign( formatter.str() );
}


bool Executor::Cook( const std::string& text , std::string* output , std::string* error ) {
    static const std::size_t kDefaultSize = 4096; // 4KB
    output->reserve( kDefaultSize );

    for( std::size_t i = 0 ; i < text.size() ; ++i ) {
        if( text[i] == '\\' ) {
            if( i+1 < text.size() && text[i+1] == '`' ) {
                output->push_back('`');
                ++i;
                continue;
            }
        }
        if( text[i] == '`' ) {
            // Start of our code execution here
            tokenizer_.Bind(text,i+1);
            if(!DoExecute( output , error) ) {
                return false;
            }
            if( tokenizer_.cur_lexme().token != TK_END ) {
                ReportError(error,"Expect \"`\" to end the code body");
                return false;
            } else {
                // Do not need to add one here,since the for loop will do this
                i = tokenizer_.position();
            }
        } else {
            output->push_back( text[i] );
        }
    }
    return true;
}

bool Executor::IsSectionEnabled( const std::string& key ) const {
    SectionMap::const_iterator i = section_var_.find(key);
    return i == section_var_.end() ? false : i->second.enabled;
}

bool Executor::EnableSection( const std::string& key ) {
    SectionMap::iterator i = section_var_.find(key);
    if( i == section_var_.end() )
        return false;
    else {
        i->second.enabled = true;
        return true;
    }
}

bool Executor::DisableSection( const std::string& key ) {
    SectionMap::iterator i = section_var_.find(key);
    if( i == section_var_.end() )
        return false;
    else {
        i->second.enabled = false;
        return true;
    }
}

void Executor::Clear() {
    // Delete all the value inside of the SoupMaker
    for( SectionMap::iterator isec =
            section_var_.begin() ; isec != section_var_.end() ; ++isec ) {
        ManduMap& m = (*isec).second.mandu_map;
        for( ManduMap::iterator imap =
                m.begin() ; imap != m.end() ; ++imap ) {
            mandu_pool_.Drop( (*imap).second );
        }
    }
    section_var_.clear();
    // Global scope
    for( ManduMap::iterator i = global_var_.begin() ;
            i != global_var_.end() ; ++i ) {
        mandu_pool_.Drop( (*i).second );
    }
    global_var_.clear();
}

Mandu* Executor::NewMandu( const std::string& section_key , const std::string& key ) {
    SectionMap::iterator i = section_var_.find( section_key );
    ManduMap* m ;

    if( i == section_var_.end() ) {
        std::pair<SectionMap::iterator,bool> slot =
            section_var_.insert( std::make_pair( section_key ,ManduSection()  ) );
        m = &((slot.first->second).mandu_map);
    } else {
        m = &((i->second).mandu_map);
    }

    std::pair<
        ManduMap::iterator,bool> iinsert = m->insert( std::make_pair(
                    key,reinterpret_cast<Mandu*>(NULL)) );
    if( iinsert.second ) {
        iinsert.first->second = mandu_pool_.Grab();
    } else {
        // First delete the old mandu there
        mandu_pool_.Drop( iinsert.first->second );
        iinsert.first->second = mandu_pool_.Grab();
    }
    return iinsert.first->second;
}

Mandu* Executor::NewMandu( const std::string& key ) {
    std::pair<
        ManduMap::iterator,bool> i = global_var_.insert( std::make_pair(
                    key,reinterpret_cast<Mandu*>(NULL)) );
    if( i.second ) {
        i.first->second = mandu_pool_.Grab();
    } else {
        mandu_pool_.Drop( i.first->second );
        i.first->second = mandu_pool_.Grab();
    }
    return i.first->second;
}


bool Executor::LookUpVariable( const std::string& section_name, const std::string& key , Mandu* mandu ) const {
    if( section_name.empty() ) {
        goto look_up_in_global;
    } else {
        SectionMap::const_iterator i = section_var_.find( section_name );
        if( i == section_var_.end() )
            goto look_up_in_global;
        else {
            ManduMap::const_iterator mandui = i->second.mandu_map.find( key );
            if( mandui == i->second.mandu_map.end() )
                goto look_up_in_global;
            else {
                mandu->Copy( *(mandui->second) );
                return true;
            }
        }
    }
look_up_in_global:
    // Lookup the variable in the global scope
    ManduMap::const_iterator i = global_var_.find( key );
    if( i != global_var_.end() ) {
        mandu->Copy( *(i->second) );
        return true;
    } else {
        return false;
    }
}

bool Executor::ParseNumber( Mandu* val , std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_NUMBER );
    long lval;
    char* pend;

    errno = 0;
    lval = ::strtol( string_to_array(
                &tokenizer_.source(),tokenizer_.position()), &pend, 10);
    if( errno != 0 ) {
        error->assign( ::strerror( errno ) );
        return false;
    }

    val->SetNumber( static_cast<int>(lval) );
    // Moving the tokenizer here
    tokenizer_.Move( pend-string_to_array(
                &tokenizer_.source(),tokenizer_.position()));
    return true;
}

bool Executor::ParseVariable( const std::string& section , Mandu* val , std::string* error ) {
   assert( tokenizer_.cur_lexme().token == TK_VARIABLE );

   // Get variable name from current stream
   std::size_t i;
   for( i = tokenizer_.position()+1 ; i < tokenizer_.source().size() ; ++i ) {
       if( !IsRestVariableChar( tokenizer_.source().at(i) ) ) {
           break;
       }
   }
   std::string var_name = tokenizer_.source().substr(
           tokenizer_.position(),i-tokenizer_.position());

   // Look up the variable in the context
   if( !LookUpVariable(section,var_name,val) ) {
       ReportError(error,"Variable:%s in section:%s is not existed!",var_name.c_str(),
               section.empty()?"<Global>":section.c_str());
       return false;
   }

   tokenizer_.Set(i);
   return true;
}

bool Executor::ParseString( Mandu* val , std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_STRING );
    // Parse the string into the Mandu buffer
    std::size_t i;
    std::string output;

    std::size_t len =0;

    // 1. First loop to decide the length of the string. ( This avoid the initial
    // copy since we are not sure that a SSO string is implemented for libc++ )
    // And typically such loop is cheaper than initial mallocation ( Suppose that
    // you have a string which is 31 bytes long, however start with 1->2-->4-->16-->32
    // which costs you 5 times malloc call).

    for( i = tokenizer_.position()+1 ; i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);

        if( cha == '\\' ) {
            if( IsStringLiteralEscapeChar(i+1) ) {
                ++len;
                ++i;
            } else {
                ++len;
            }
        } else {
            if( cha == '\"' ) {
                break;
            } else {
                ++len;
            }
        }
    }

    if( i == tokenizer_.source().size() ) {
        ReportError(error,"The string literal is not closed by \"");
        return false;
    }

    output.reserve(len);

    // 2. Copy the data into the buffer
    for( i = tokenizer_.position()+1 ; i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);

        if( cha  == '\\' ) {
            if( IsStringLiteralEscapeChar( i+1 ) ) {
                output.push_back( tokenizer_.source().at(i+1) );
                i++;
            } else {
                output.push_back( cha );
            }
        } else {
            if( tokenizer_.source().at(i) == '\"' ) {
                break;
            } else {
                output.push_back(cha);
            }
        }
    }
    val->SetString(output);
    // Move the tokenizer
    tokenizer_.Set(i+1);
    return true;
}

bool Executor::ParseAtomic( const std::string& section , Mandu* val , std::string* error ) {
    switch( tokenizer_.cur_lexme().token ) {
        case TK_NUMBER:
            return ParseNumber(val,error);
        case TK_STRING:
            return ParseString(val,error);
        case TK_VARIABLE:
            return ParseVariable(section,val,error);
        default:
            UNREACHABLE(return false);
    }
}

int Executor::ParseListElement( const std::string& section , Mandu* from , Mandu* to ,
       std::list<Mandu*>* output , std::string* error  ) {
    // ListElement means a single element in the list, it optionally can be a range value or
    // a single value. The single value could be an atomic value or another list
    switch( tokenizer_.cur_lexme().token ) {
        case TK_NUMBER:
        case TK_STRING:
        case TK_VARIABLE:
            if( !ParseAtomic(section,from,error) )
                return ELEMENT_FAIL;
            break;

        case TK_LSQR:
            return ParseList(section,output,error) ? ELEMENT_LIST : ELEMENT_FAIL;
        default:
            UNREACHABLE(return ELEMENT_FAIL);
    }
    // Now cehck if we have optional - to indicate it is a range operation
    if( tokenizer_.cur_lexme().token == TK_SUB ) {
        // It is a range operation here, checking the from must be a number
        if( from->type() != Mandu::TYPE_NUMBER ) {
            ReportError(error,"The range operation must comes with 2 number operands");
            return ELEMENT_FAIL;
        }
        // Parsing the next one
        tokenizer_.Move();
        if( !ParseAtomic(section,to,error) )
            return ELEMENT_FAIL;
        else {
            // Checking whether the to mandu is a number or not
            if( to->type() != Mandu::TYPE_NUMBER ) {
                ReportError(error,"The range operation must comes with 2 number operands");
                return ELEMENT_FAIL;
            } else {
                if( from->ToNumber() >= to->ToNumber() ) {
                    ReportError(error,"The left hand operand of range MUST BE "
                                      "LESS than the right hand operand of range!");
                    return ELEMENT_FAIL;
                }
            }
        }
        return ELEMENT_RANGE;
    } else {
        return ELEMENT_ATOMIC;
    }

}

bool Executor::ParseList( const std::string& section , std::list<Mandu*>* outputs , std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_LSQR );
    tokenizer_.Move();
    // Quick test for empty list and then report error
    if( tokenizer_.cur_lexme().token == TK_RSQR ) {
        ReportError(error,"Empty list,what's the point!");
        return false;
    }

    Mandu* from = NULL;
    Mandu* to = NULL;

    do {
        from = from == NULL ? mandu_pool_.Grab() : from;
        to = to == NULL ? mandu_pool_.Grab() : to;

        int type = ParseListElement(section,from,to,outputs,error);
        if( type == ELEMENT_FAIL ) {
            mandu_pool_.Drop(from);
            mandu_pool_.Drop(to);
            goto fail;
        } else {
            switch(type) {
                case ELEMENT_ATOMIC:
                    outputs->push_back( from );
                    from = to;
                    to = NULL;
                    break;
                case ELEMENT_RANGE:
                    {
                        int from_count = from->ToNumber();
                        int to_count = to->ToNumber();
                        for( ; from_count < to_count ; ++from_count ) {
                            outputs->push_back(
                                    mandu_pool_.Grab(from_count));
                        }
                        mandu_pool_.Drop(from);
                        mandu_pool_.Drop(to);
                        from = to = NULL;
                        break;
                    }
                case ELEMENT_LIST:
                    break;
                default:
                    UNREACHABLE(return false);
            }
            if( tokenizer_.cur_lexme().token == TK_COMMA ) {
                // Continue looping
                tokenizer_.Move();
                continue;
            } else if( tokenizer_.cur_lexme().token == TK_RSQR ) {
                tokenizer_.Move();
                break;
            } else {
                ReportError(error,"Unexpected element in list");
                return false;
            }
        }
    } while(true);
    return true;
fail:
    for( std::list<Mandu*>::iterator i = outputs->begin() ;  i != outputs->end() ; ++i ) {
        mandu_pool_.Drop( *i );
    }
    outputs->clear();
    return false;
}

bool Executor::ExecuteList( const std::string& section , std::list<std::string>* outputs , std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_LSQR );
    std::list<Mandu*> list;
    if( !ParseList(section,&list,error) )
        return false;

    // Check wether we need to execute the body or just output the string here
    if( tokenizer_.cur_lexme().token == TK_LBRA ) {
        std::string dummy;

        int beg_pos = tokenizer_.position();
        int end_pos = -1;

        for( std::list<Mandu*>::iterator ib = list.begin() ; ib != list.end() ; ++ib ) {
            outputs->push_back(dummy);
            std::string* temp = &(outputs->back());
            if( !ExecuteBody( **ib , temp , error ) ) {
                goto fail;
            }
            if( end_pos == -1 ) {
                end_pos = tokenizer_.position();
            }
            tokenizer_.Set(beg_pos);
        }

        assert( end_pos != -1 );
        tokenizer_.Set(end_pos);
    } else {
        // Just dump the list into the outptus string buffer is fine
        for( std::list<Mandu*>::iterator ib = list.begin() ; ib != list.end() ; ++ib ) {
            outputs->push_back( (*ib)->ConvertToString() );
        }
    }
    return true;

fail:
    for( std::list<Mandu*>::iterator i = list.begin() ;  i != list.end() ; ++i ) {
        mandu_pool_.Drop( *i );
    }
    return false;
}

bool Executor::ExecuteAtomic( const std::string& section , std::list<std::string>* outputs , std::string* error ) {
    Mandu* atomic = mandu_pool_.Grab();
    if( !ParseAtomic(section,atomic,error) )
        goto fail;

    if( tokenizer_.cur_lexme().token == TK_LBRA ) {
        outputs->push_back(std::string());
        std::string* temp = &(outputs->back());
        if(!ExecuteBody(*atomic,temp,error)) {
            goto fail;
        }
        mandu_pool_.Drop(atomic);
        return true;
    } else {
        outputs->push_back( atomic->ConvertToString() );
        mandu_pool_.Drop(atomic);
        return true;
    }

fail:
    mandu_pool_.Drop(atomic);
    return false;
}

bool Executor::ExecuteBody( const Mandu& dollar_sign , std::string* output , std::string* error ) {
    assert( tokenizer_.cur_lexme().token == TK_LBRA );
    tokenizer_.Move();
    for( std::size_t i = tokenizer_.position() ; i < tokenizer_.source().size() ; ++i ) {
        int cha = tokenizer_.source().at(i);
        if( cha == '\\' ) {
            if( IsBodyEscapeChar(i+1) ) {
                // It is the escape character we need to skip here
                output->push_back(cha);
                ++i;
            } else {
                output->push_back(cha);
            }
        } else {
            if( cha == '$' ) {
                // Do the substitution here
                output->append(dollar_sign.ConvertToString());
            } else {
                // End of the stream here
                if( cha == '}' ) {
                    // End of the body expression here. Just return here
                    tokenizer_.Set( i+1 );
                    return true;
                }
                output->push_back(cha);
            }
        }
    }
    // If we reach here , it means we meet an unexceptional EOF of the stream
    ReportError(error,"Unexpected end of the stream!Expecting \"}\"");
    return false;
}

bool Executor::Execute( std::list<std::string>* output , std::string* error ) {
    Mandu* section_key = mandu_pool_.Grab();
    std::string dummy;

    if( tokenizer_.cur_lexme().token == TK_SECTION_START ) {
        // Parsing the section key here
        tokenizer_.Move();
        if( tokenizer_.cur_lexme().token != TK_STRING ) {
            ReportError(error,"Expect section key!");
            goto fail;
        }
        if( !ParseString( section_key , error ) )
            goto fail;

        // Now we have got our section key , just start the execution here
        if( tokenizer_.cur_lexme().token == TK_END ) {
            ReportError(error,"Unexpected end of the stream with empty section body!");
            goto fail;
        }

        // Now just check whether such section key is existed or not
        if( !IsSectionEnabled(section_key->ToString()) ) {
            SectionSkipper skipper(
                    tokenizer_.source(), tokenizer_.position() );
            int ret = skipper.Skip(error);
            if( ret < 0 ) {
                // We have met an error just return here
                goto fail;
            } else {
                tokenizer_.Set(ret);
                // Now we have correctly skipped the body , just return
                return true;
            }
        }
    }

    // Start to run the body here
    do {
        switch( tokenizer_.cur_lexme().token ) {
            case TK_NUMBER:
            case TK_STRING:
            case TK_VARIABLE:
                if( !ExecuteAtomic(section_key->type() == Mandu::TYPE_NONE ?
                            dummy : section_key->ToString() , output, error)) {
                    goto fail;
                }
                break;
            case TK_LSQR:
                if( !ExecuteList(section_key->type() == Mandu::TYPE_NONE ?
                            dummy : section_key->ToString() , output, error)) {
                    goto fail;
                }
                break;
            default:
                return true;
        }
        // Now check whether we can exit the loop or not
        switch( tokenizer_.cur_lexme().token ) {
            case TK_END:
                return true;
            case TK_SECTION_END:
                tokenizer_.Move();
                return true;
            default:
                break;
        }
    } while(true);

fail:
    mandu_pool_.Drop(section_key);
    return false;
}

void Executor::Concatenate( const std::list<std::string>& input, std::string* output ) {
    // 1. First loop to checkout how many memory is needed for string
    std::size_t len = 0;
    for( std::list<std::string>::const_iterator ib = input.begin() ;
            ib != input.end() ; ++ib ) {
        len += ib->size();
    }

    output->reserve(len+output->size());
    for( std::list<std::string>::const_iterator ib = input.begin() ;
            ib != input.end() ; ++ib ) {
        output->append( *ib );
    }
}

bool Executor::DoExecute( std::string* output, std::string* error ) {
    std::list<std::string> ol;
    do {
        if( !Execute(&ol,error) )
            return false;
        if( tokenizer_.cur_lexme().token == TK_END ) {
            break;
        }
    } while( true );

    // Stringtify all the input inside of the string table
    Concatenate(ol,output);
    return true;
}
} //namespace detail

void Mandu::SetList( const std::vector<Mandu*>& l ) {
    Detach();
    type_ = TYPE_LIST;
    std::vector<Mandu*>* out = ::new (mandu_list_buf_) std::vector<Mandu*>();
    for( std::size_t i = 0 ; i < l.size() ; ++i ) {
        out->push_back( l[i] );
    }
}

void Mandu::Copy( const Mandu& mandu ) {
    switch( mandu.type_ ) {
        case TYPE_NONE:
            Detach();
            type_ = TYPE_NONE;
            return;
        case TYPE_NUMBER:
            SetNumber(mandu.ToNumber());
            return;
        case TYPE_STRING:
            SetString(mandu.ToString());
            return;
        case TYPE_LIST:
            SetList( mandu.ToList() );
            return;
        default:
            UNREACHABLE(return);
    }
}

std::string Mandu::ConvertToString() const {
    switch( type_ ) {
        case TYPE_NONE:
            return std::string("<:null:>");
        case TYPE_NUMBER:
            {
                char buf[256];
                sprintf(buf,"%d",ToNumber());
                return std::string(buf);
            }
        case TYPE_LIST:
            {
                std::string output;
                const std::vector<Mandu*>& l = ToList();
                for( std::size_t i = 0 ; i < l.size() ; ++i ) {
                    l[i]->AppendString(&output);
                }
                return output;
            }
        case TYPE_STRING:
            return ToString();
        default:
            UNREACHABLE(return std::string());
    }
}

// =======================================================
// SoupMaker
// =======================================================

SoupMaker::SoupMaker():
    impl_( new detail::Executor() )
{}

SoupMaker::~SoupMaker() {
    delete impl_;
}

bool SoupMaker::IsSectionEnabled( const std::string& key ) {
    return impl_->IsSectionEnabled(key);
}

bool SoupMaker::EnableSection( const std::string& key ) {
    return impl_->EnableSection( key );
}

bool SoupMaker::DisableSection( const std::string& key ) {
    return impl_->DisableSection( key );
}

void SoupMaker::Clear() {
    impl_->Clear();
}

Mandu* SoupMaker::NewMandu( const std::string& section_key , const std::string& key ) {
    return impl_->NewMandu( section_key , key );
}

Mandu* SoupMaker::NewMandu( const std::string& key ) {
    return impl_->NewMandu(key);
}

bool SoupMaker::Cook( const std::string& text , std::string* output , std::string* error ) {
    return impl_->Cook( text,output,error );
}
}// namespace mandu


