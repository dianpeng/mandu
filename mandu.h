#ifndef MANDU_H_
#define MANDU_H_

#include <cstddef>
#include <string>
#include <vector>
#include <cassert>

// Mandu
// A very tiny C++ text template engine. Its goal is to provide a
// small yet powerful embeded C++ template engine for task like HTML
// template processing.

namespace mandu {
namespace detail {
// Implementator for the SoupMaker
class Executor;
// Zone Allocator
template< typename T > class ZoneAllocator;
}// namespace detail

class Mandu;
class SoupMaker;

class Mandu {
public:

    enum {
        TYPE_NONE,
        TYPE_STRING,
        TYPE_NUMBER,
        TYPE_LIST
    };

    ~Mandu() {
        Detach();
    }

    const std::string& ToString() const {
        assert( type() == TYPE_STRING );
        return *reinterpret_cast<const std::string*>(
                string_buf_);
    }

    int ToNumber() const {
        assert( type() == TYPE_NUMBER );
        return number_;
    }

    const std::vector<Mandu*>& ToList() const {
        assert( type() == TYPE_LIST );
        return *reinterpret_cast<
            const std::vector<Mandu*>*>( mandu_list_buf_ );
    }

    std::string ConvertToString() const;

    void SetString( const std::string& str ) {
        Detach();
        type_ = TYPE_STRING;
        ::new (string_buf_) std::string(str);
    }

    void SetNumber( int number ) {
        Detach();
        type_ = TYPE_NUMBER;
        number_ = number;
    }

    void SetList( const std::vector<Mandu*>& list );

    int type() const {
        return type_;
    }

    void Swap( std::string* string ) {
        assert( type() == TYPE_STRING );
        reinterpret_cast<std::string*>(
                string_buf_)->swap(*string);
    }

    void Swap( std::vector<Mandu*>* mandu_list ) {
        assert( type() == TYPE_LIST );
        reinterpret_cast<std::vector<Mandu*>*>(
                mandu_list_buf_)->swap(*mandu_list);
    }

    // This function will do a half shallow copy. If the mandu
    // contains string or number then it is a real copy, if it
    // is a list, then the Mandu inside of that list will not
    // be copied but instead sharing between those two mandu
    // directly. User typically doesn't call FreeMandu explicitly
    // since the SoupMaker takes care of everything.
    void Copy( const Mandu& mandu );

private:

    // This function i used to format string by append function
    void AppendString( std::string* output ) {
        output->append( ConvertToString() );
    }

    void Detach() {
        // Explicit call destructor seems not working with fullname( namespace prefix )
        using std::string;
        using std::vector;

        switch( type_ ) {
            case TYPE_NONE:
            case TYPE_NUMBER:
                return;
            case TYPE_STRING:
                reinterpret_cast<std::string*>(string_buf_)->~string();
                return;
            case TYPE_LIST:
                reinterpret_cast<
                    std::vector<Mandu*>*>(mandu_list_buf_)->~vector<Mandu*>();
                return;
            default:
                assert(0);
                return;
        }
    }

private:
    // We only allow assignment operator for this class since we force
    // the user to allocate this class in the factory class (SoupMaker)
    // User can use Copy _NOT_ assignment operator( which I don't like it )
    // to get the deep copy of this objects.

    Mandu():
        type_( TYPE_NONE )
    {}

    Mandu( int number ) {
        SetNumber(number);
    }

    Mandu( const std::string& str ) {
        SetString(str);
    }

    Mandu( const std::vector<Mandu*>& list ) {
        SetList(list);
    }

private:
    union {
        char mandu_list_buf_[sizeof( std::vector<Mandu*> )];
        char string_buf_[sizeof(std::string)];
        int number_;
    };

    int type_;

    Mandu( const Mandu& );
    void operator =( const Mandu& );
    friend class detail::Executor;
    friend class detail::ZoneAllocator<Mandu>;
};

class SoupMaker {
public:
    SoupMaker();
    ~SoupMaker();

    // Create a new Mandu that binds not type
    Mandu* NewMandu( const std::string& section , const std::string& key );
    Mandu* NewMandu( const std::string& key );
    Mandu* NewMandu();

    // The section related operations
    bool EnableSection( const std::string& section_name );
    bool DisableSection( const std::string& section_name );
    bool IsSectionEnabled( const std::string& section_name );

    // This function will clear any states of the SoupMaker object and all the
    // Mandu objects you New from this SoupMaker becomes invalid. You should
    // re-new any objects you want to use in later phase
    void Clear();

    // Cook the mandu soup with existed settings. This will perform the real template text
    // substitution here. The output will be stored inside of the output and also if any
    // error is happened, the error string will store the description
    bool Cook( const std::string& txt , std::string* output , std::string* error );

private:
    void operator = ( const SoupMaker& );
    SoupMaker( SoupMaker& );

    detail::Executor* impl_;
};
} // mandu
#endif // MANDU_H_





