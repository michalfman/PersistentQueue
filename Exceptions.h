
#ifndef EXCEPTIONS_H_
#define EXCEPTIONS_H_


#include <iostream>
#include <exception>

using namespace std;

class EmptyException : public exception
{
  const char * what () const throw () {
    return "Empty Exception";
  }
};

#endif /* EXCEPTIONS_H_ */

