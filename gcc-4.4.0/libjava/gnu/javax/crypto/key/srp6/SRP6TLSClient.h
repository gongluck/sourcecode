
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_crypto_key_srp6_SRP6TLSClient__
#define __gnu_javax_crypto_key_srp6_SRP6TLSClient__

#pragma interface

#include <gnu/javax/crypto/key/srp6/SRP6KeyAgreement.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace javax
    {
      namespace crypto
      {
        namespace key
        {
            class IncomingMessage;
            class OutgoingMessage;
          namespace srp6
          {
              class SRP6TLSClient;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace security
    {
        class KeyPair;
    }
  }
}

class gnu::javax::crypto::key::srp6::SRP6TLSClient : public ::gnu::javax::crypto::key::srp6::SRP6KeyAgreement
{

public:
  SRP6TLSClient();
public: // actually protected
  virtual void engineInit(::java::util::Map *);
  virtual ::gnu::javax::crypto::key::OutgoingMessage * engineProcessMessage(::gnu::javax::crypto::key::IncomingMessage *);
  virtual void engineReset();
private:
  ::gnu::javax::crypto::key::OutgoingMessage * sendIdentity(::gnu::javax::crypto::key::IncomingMessage *);
public: // actually protected
  virtual ::gnu::javax::crypto::key::OutgoingMessage * computeSharedSecret(::gnu::javax::crypto::key::IncomingMessage *);
private:
  ::java::lang::String * __attribute__((aligned(__alignof__( ::gnu::javax::crypto::key::srp6::SRP6KeyAgreement)))) I;
  JArray< jbyte > * p;
  ::java::security::KeyPair * userKeyPair;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_crypto_key_srp6_SRP6TLSClient__
