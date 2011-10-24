/**
 *  This file is derived from sequence.H which is licensed without
 *  restriction.   It has been modified to use boost and
 *  simplify the template parameters / enforce relationship between
 *  the signed/unsigned types.  
 *
 *  It has also been modified to fit my style.
 *
 */

// Original Copy Right Notice
//
// sequence.H   -- Sequence Arithmetic Framework
//  
//  Copyright (C) 2005 Enbridge Inc.
// 
//
//  This file is part of the Sequence Arithmentic Framework.  This library is
//  free software; you can redistribute it and/or modify it under the terms of
//  the GNU General Public License as published by the Free Software Foundation;
//  either version 2, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
//  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
//  details.
//
//  You should have received a copy of the GNU General Public License along
//  with this library; see the file COPYING.  If not, write to the Free
//  Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
//  USA.
//
//  As a special exception, you may use this file as part of a free software
//  library without restriction.  Specifically, if other files instantiate
//  templates or use macros or inline functions from this file, or you compile
//  this file and link it with other files to produce an executable, this
//  file does not by itself cause the resulting executable to be covered by
//  the GNU General Public License.  This exception does not however
//  invalidate any other reasons why the executable file might be covered by
//  the GNU General Public License.


#ifndef _TORNET_SEQUENCE_HPP
#define _TORNET_SEQUENCE_HPP

// 
// sequence        -- a namespace containing the types used for sequence number aritmetic
// sequence::number    -- a sequence numbering type providing basic arithmetic operations and comparisons
// sequence::ordering    -- an ordering type over some basic sequence number
// 
///     Implements sequence number arithmetic for any basic C++ type with a "signed" and
/// "unsigned" version (any of the integer types).
/// 

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>
#include <boost/type_traits/make_unsigned.hpp>
#include <boost/type_traits/make_signed.hpp>

// 
// namespace sequence
// 
/// 
///     Sequence Arithmetic over signed type S, and unsigned type U.
/// 
namespace sequence {

    // 
    // sequence::number        -- operations on unsigned sequence numbers
    // 
    ///    Simple operations on N-bit sequence numbers, with no additional context.  Sequence
    /// numbers within 2^(N-1)-1 "greater" (in circular terms) are considered "greater", and within
    /// 2^(N-1) "less" (in circular terms) are considered "less".  Therefore, distances between two
    /// sequence numbers can always be represented by a N-bit signed number.
    /// 
    ///    This class must be instantiated with a pair of simple arithmetic types, one signed and
    /// one unsigned.
    /// 
    template < typename T = signed short >
    class number {
    protected:
      typedef typename boost::make_signed<T>::type S;
      typedef typename boost::make_unsigned<T>::type U;
      U            _curr;

    public:
      number( U    sequence = 0)
      : _curr( sequence ) {  }

    //
    // sequence::number::value()        -- Extract the raw unsigned sequence number
    // sequence::number::operator U
    // 
    U value() const { return _curr; }
      operator U() const { return value(); }


    template<typename Stream>
    friend Stream& operator << ( Stream& s, const number& n ) {
      s.write( (char*)&n._curr, sizeof(n._curr) );
      return s;
    }
    template<typename Stream>
    friend Stream& operator >> ( Stream& s, number& n ) {
      s.read((char*)&n._curr, sizeof(n._curr) );
      return s;
    }
    operator std::string()const { return boost::lexical_cast<std::string>(_curr); }

    // 
    // sequence::number::distance        -- Distance to another sequence number
    // 
    //     Defines an N-bit sequence numbering space, where the domain of all sequence numbers
    // are defined for comparison.
    // 
    //     For example, often "Sequence Number Arithmetic" is only defined for 16-bit sequence
    // numbers up to 2^15-1 (32767) apart; two sequence numbers exactly 2^15 (32768) apart are
    // undefined when compared (see RFC 1982: Serial Number Arithmetic).  However, we treat
    // the sequence number space as being "asymmetrical"; there is one more sequence number
    // "less than" us, than there is "greater than".  This is exactly the same as the
    // definition of the "signed" vs. "unsigned" interpretation of the same integer.
    // 
    //     For example, if we compare all 16-bit sequence numbers against sequence number 0:
    // 
    // unsigned      binary    signed
    // sequence      value     distance
    //    32767   == 0x7fff ==   32767
    //        1   == 0x0001 ==       1
    //        0   == 0x0000 ==       0
    //    65535   == 0xffff ==      -1 
    //    65534   == 0xfffe ==      -2
    //    32768   == 0x8000 ==  -32768
    // 
    //     It is easy to see that the signed interpretation of the sequence numbers are in the
    // correct order, so long as we "rotate" the sequence number in question so that it's 0
    // matches up with the base sequence number we are comparing it against.
    // 
    // 
    //     Therefore, we can use simple unsigned subtraction, converted to 16-bit signed, to
    // determine what distance a sequence number is from some other sequence number.  We
    // simply subtract the base sequence number ("rotating" the sequence number's space, so
    // that 0 it its space is equal to the base).  Then, if it was "greater" than the base
    // sequence number, it will leave a positive (signed) result.  If it was "less", its
    // signed result will be negative.
    // 
    S distance( U sequence )const  { return S( sequence - _curr ); }
    S operator-( U sequence )const { return distance( sequence );  }

    number& operator++()    { ++_curr; return *this; }
    number& operator++(int) { ++_curr; return *this; }
    number& operator--()    { --_curr; return *this; }
    number& operator--(int) { --_curr; return *this; }
    number operator+(int v)const { return number(_curr+v); }
    number operator-(int v)const { return number(_curr-v); }

    // 
    // </<=/==/>=/>        -- Logical operators between sequence numbers. 
    // 
    //     Comparisons between sequence numbers are related by the "inverse" of the "distance"
    // to that sequence number.  In other words, if the distance to another sequence number is
    // +'ve, then we are "less than" that sequence number.
    // 
    bool operator< ( U sequence ) const {return distance( sequence ) >  0; }
    bool operator<=( U sequence ) const {return distance( sequence ) >= 0; }
    bool operator==( U sequence ) const {return distance( sequence ) == 0; }
    bool operator>=( U sequence ) const {return distance( sequence ) <= 0; }
    bool operator> ( U sequence ) const {return distance( sequence ) <  0; }
    }; // sequence::number

/** 
 sequence::ordering

     Provide for a total ordering of sequence numbers which wrap (for example) in 16-bits.
 Support increment and assignment of 16-bit sequence numbers while maintaining ascending
 total order.  Based on sequence::number.
 
     In contexts expecting a (long long), returns the total order.  In contexts expecting an
 (unsigned short), returns the simple sequence number.  Be careful of the context in which
 you use a sequence::ordering, especially in comparisons; ensure that you use
 
         sequence::ordering     </>/==     <unsigned>
 
 to get comparisons in the total order;
 
         <unsigned>        </>/==    sequence::ordering
 
 will compare based on simple 16-bit sequence numbers.
 
     Since 64-bit signed long long integers are used (by default) for total ordering, the
 usable total ordering space is 2^63 (excluding sign bit).  We use 2^16 of the space on each
 "wrap" of our sequence numbers, so we can maintain order over ( 2^63 / 2^16 ) wraps.  If we
 "wrapped" our sequence numbering once per second, we can maintain ordering for
 
         ( 2^63 / 2^16 ) / ( 60 * 60 * 24 *365 ) == 4,462,756 years.

     This should be enough for most applications, since sequence::ordering should ONLY be used
 to manage a pool of "currently" existing sequence numbered objects, all within one "wrap" of
 our actual sequence numbers of each-other.
 
 */
template < typename T =   signed short, typename L = long long >    
class ordering : public number<T> {
    typedef typename number<T>::U U;
    typedef typename number<T>::S S;
        L            _base;        // base for total ordering

    public:
    std::ostream           &output( std::ostream &lhs ) const {
        return lhs << std::setw( 10 ) << (L) *this
               << " (_base: " << std::setw( 8 ) << this->_base 
               << ", _curr: " << std::setw( 5 ) << this->_curr << ")";
    }

    ordering( U seq = 0, L off = 0 )
    : number<T>( seq ), _base( off ) { }

    ordering( const ordering &rhs )
    : number<T>( rhs._curr ), _base( rhs._base ) {  }

    ordering &operator=( const ordering &rhs ) {
        this->_base            = rhs._base;
        this->_curr            = rhs._curr;
        return *this;
    }

    // 
    // (L)<ordering>        -- return the total ordering
    // (U)<ordering>        -- return just the sequence number
    // (*)<ordering>        -- Invalid!  Don't use.
    // 
    //     Return the simple sequence number, or the total order.
    // 
    //     NOTE: If the object is used in some other numeric environment, it will NOT be clear
    // what cast operator will be used; DO NOT do that!  Make it clear what is expected, by
    // using ONLY in (U) or (L) contexts!  "When in Rome, ...".
    // 
    operator U() const { return this->_curr; }
    operator L() const { return this->_base + this->_curr; }

    // 
    // ordering::order        -- total order of the given sequence number
    // 
    //     Calculate the total ordering of the given sequence number, relative to the current
    // sequence number.  It just calculates which "side" the given sequence number is on: is
    // it within 32K BELOW, or is it within 32K ABOVE the current sequence number?
    // 
    // RETURN VALUE
    // 
    //     A <L> value usable for total ordering, which will be <, ==, or > when
    // compared against this object.  May be -'ve (but still valid when compared to other
    // total order values), if this object's _base is < 64K.
    // 
    //     We used to do a complex calculation here, to detect wrap-around in sequence
    // arithmetic, but it turns out to be trivial (and more correct) to use signed/unsigned
    // numbers (see sequence::number::distance).
    // 
    L order( U sequence ) const { return this->_base + this->_curr + distance( sequence ); }

    // 
    //   <ordering>.assign( <U> )
    //   <ordering> = <U>
    //   <ordering>++
    // ++<ordering>
    // 
    //     Advancing sequence numbers.  Maintains a total order, which is NOT ALLOWED to go
    // backwards!  Therefore, assignement of a very divergent sequence number assumes that
    // numbering has wrapped, and advances the "_base" used for total ordering.
    // 
    ordering &assign( U seq ) {
        // Calculate the new sequence number's total order relative ourself
        L curord    = (L)*this;
        L seqord    = order( seq );

        // OK, assign the new sequence number. 
        // 
        // Account for "out of order" sequence numbers.  If the new sequence number's total
        // order was on the "wrong" side, we'll assume that numbering has been broken, and
        // we'll advance _base to ensure that the new total order always advances.  Otherwise,
        // we'll assume for that it is in valid, ascending total order; therefore, if it is
        // lower in 16 bits, then we need to advance our _base (so our total order advances)

        if ( seqord        < curord                // 'sequence' is out of order
         || seq     < this->_curr ) {            //   or in order, but has wrapped!
        this->_base               += L( 1 ) << ( 8 * sizeof U() );    // eg. 0x10000, if U is <unsigned short>
        }
        this->_curr            = seq;
        return *this;
    }

    ordering& operator=( U seq ) { return assign( seq ); }
 
    ordering& operator++() {               // preincrement.  Simplified case of assign().
      if ( ++this->_curr == 0 ) {
        this->_base        += L( 1 ) << ( 8 * sizeof U() );
      }
      return *this;
    }

    ordering operator++( int )    // postincrement.  Use temporary and preincrement.
    {
        ordering        tmp( *this );
        ++*this;
        return tmp;
    }

    // 
    // <ordering> == <ordering>
    // <ordering> <  <ordering>
    // 
    //     For std:: containers of <ordering>.  Uses total ordering to compare between
    // instances of <ordering>.
    // 
    bool operator==( const ordering &rhs ) const {
        return (L)( *this ) == (L)( rhs );
    }
    bool operator<( const ordering     &rhs ) const {
        return (L)( *this ) < (L)( rhs );
    }

    // 
    // <ordering> == <U>
    // <ordering> <  <U>
    //          ...
    // 
    //     For direct comparison against <U> sequence numbers.  Uses total ordering of
    // the provided <U> sequence number on the RHS, against the <ordering> LHS, based
    // from the <ordering>.
    // 
    bool operator==( U seq ) const {
        return seq == this->_curr;
    }
    bool operator<( U rhs ) const {
        return (L)( *this ) <  order( rhs );
    }
    bool operator<=( U rhs ) const {
        return (L)( *this ) <= order( rhs );
    }
    bool operator>( U rhs ) const {
        return (L)( *this ) >  order( rhs );
    }
    bool operator>=( U rhs ) const {
        return (L)( *this ) >= order( rhs );
    }

    // 
    // ordering::monotonic        -- Calculate the monotonic (increasing) order of the given sequence
    // ordering::monotonicdistance    -- What is the monotonic distance of the given sequence, from ours
    // 
    //     Converts the "wrapping" N-bit sequence numbers into a monotonic value (one that
    // can always be compared to other monotonic sequence values (so long as the same _base
    // was used!), without concern for "wrapping".  In other words, where < and > always have
    // the usual meaning.
    //     
    //     Differs from ordering::order in that the sequence numbers are always considered to
    // be on the "greater" side of the current sequence number; if they are not within the
    // next 32K, then the sequence number is considered "out of order", and is "wrapped" into
    // the next 64K range (in other words, consider it to be >64K "greater", rather than <32K
    // "less".)
    // 
    //     Yes, I know; monotonic means "x < y ==> F(x) < F(y)"; for 16-bit sequence numbers,
    // this clearly is almost never true, for any of the usually monotonic functions F -- but
    // for long long sequence numbers, for all practical purposes, we can assume that the
    // sequence numbers never wrap, and thus we can use normal monotonic functions to operate
    // on the sequence numbers.
    // 
    L monotonic( U seq ) const {
        L            curord    = (L) *this;
        L            seqord    = order( seq );
        if ( seqord < curord )                    // Is the given sequence "before" our sequence?
        seqord               += L( 1 ) << ( 8 * sizeof U() ); //   Yup; wildly out of order.  Wrap it.

        return seqord;
    }

    L monotonicdistance( U seq ) const {
        return monotonic( seq ) - (L) *this;
    }
 }; // sequence::ordering

} // sequence

template < typename S, typename L > 
inline std::ostream &operator<<( std::ostream &lhs, const sequence::ordering<S,L>& rhs ) {
    return rhs.output( lhs );
}

#endif // _INCLUDE_SEQUENCE_H
