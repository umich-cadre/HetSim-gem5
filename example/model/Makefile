PYTHON_PATH := /usr/include/python2.7
MODULENAME := params

default:	$(MODULENAME).h
	swig -python -module $(MODULENAME) $(MODULENAME).h
	gcc -c -fpic $(MODULENAME)_wrap.c -I$(PYTHON_PATH) -o $(MODULENAME)_wrap.o
	gcc -shared $(MODULENAME)_wrap.o -o _$(MODULENAME).so

.PHONY: clean

clean:
	$(RM) _$(MODULENAME).so $(MODULENAME)*.o $(MODULENAME).py* $(MODULENAME)_wrap.c
