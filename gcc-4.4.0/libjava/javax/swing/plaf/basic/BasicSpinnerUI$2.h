
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_plaf_basic_BasicSpinnerUI$2__
#define __javax_swing_plaf_basic_BasicSpinnerUI$2__

#pragma interface

#include <java/awt/event/MouseAdapter.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
      namespace event
      {
          class MouseEvent;
      }
    }
  }
  namespace javax
  {
    namespace swing
    {
        class Timer;
      namespace plaf
      {
        namespace basic
        {
            class BasicSpinnerUI;
            class BasicSpinnerUI$2;
        }
      }
    }
  }
}

class javax::swing::plaf::basic::BasicSpinnerUI$2 : public ::java::awt::event::MouseAdapter
{

public: // actually package-private
  BasicSpinnerUI$2(::javax::swing::plaf::basic::BasicSpinnerUI *);
public:
  void mousePressed(::java::awt::event::MouseEvent *);
  void mouseReleased(::java::awt::event::MouseEvent *);
public: // actually package-private
  void increment();
  jboolean volatile __attribute__((aligned(__alignof__( ::java::awt::event::MouseAdapter)))) mouseDown;
  ::javax::swing::Timer * timer;
  ::javax::swing::plaf::basic::BasicSpinnerUI * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_plaf_basic_BasicSpinnerUI$2__
