
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_management_Server$ServerInfo__
#define __gnu_javax_management_Server$ServerInfo__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace gnu
  {
    namespace javax
    {
      namespace management
      {
          class Server;
          class Server$ServerInfo;
      }
    }
  }
  namespace javax
  {
    namespace management
    {
        class ObjectInstance;
    }
  }
}

class gnu::javax::management::Server$ServerInfo : public ::java::lang::Object
{

public:
  Server$ServerInfo(::gnu::javax::management::Server *, ::javax::management::ObjectInstance *, ::java::lang::Object *);
  virtual ::java::lang::Object * getObject();
  virtual ::javax::management::ObjectInstance * getInstance();
private:
  ::javax::management::ObjectInstance * __attribute__((aligned(__alignof__( ::java::lang::Object)))) instance;
  ::java::lang::Object * object;
public: // actually package-private
  ::gnu::javax::management::Server * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_management_Server$ServerInfo__