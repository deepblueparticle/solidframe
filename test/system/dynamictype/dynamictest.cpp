#include "system/dynamictype.hpp"
#include "testcommands.hpp"
#include "system/debug.hpp"
#include "system/cassert.hpp"

#include <vector>

namespace test_cmds{

class FirstCommand;
class SecondCommand;
class ThirdCommand;//Inherits Second

}


class MyObject{
	DYNAMIC_RECEIVER_DECLARATION;
public:
	
	static void registerCommands();
	void receiveCommand(test_base::BaseCommand &);
private:
	
	int receive(test_cmds::FirstCommand &_rcmd);
	int receive(test_cmds::SecondCommand &_rcmd);
};

DYNAMIC_RECEIVER_DEFINITION(MyObject, receive, test_base::BaseCommand);

#include "testcommands.hpp"

void MyObject::registerCommands(){
	registerCommand<MyObject, test_cmds::FirstCommand>();
	registerCommand<MyObject, test_cmds::SecondCommand>();
	registerCommand<MyObject, test_cmds::ThirdCommand>();
}


void MyObject::receiveCommand(test_base::BaseCommand &_rcmd){
	executeCommand(_rcmd, this);
}

int MyObject::receive(test_cmds::FirstCommand &_rcmd){
	idbg("First command value "<<_rcmd.v);
	return 0;
}

int MyObject::receive(test_cmds::SecondCommand &_rcmd){
	idbg("Second command value "<<_rcmd.v);
	return 0;
}


int main(){
#ifdef UDEBUG
	Dbg::instance().levelMask();
	Dbg::instance().moduleMask();
	Dbg::instance().initStdErr(false);
#endif

	MyObject::registerCommands();
	MyObject o;
	
	test_cmds::FirstCommand		c1(10);
	test_cmds::SecondCommand	c2("second");
	test_cmds::ThirdCommand		c3('3', "third", 30);
	
	o.receiveCommand(c1);
	o.receiveCommand(c2);
	o.receiveCommand(c3);
	
	return 0;
}

