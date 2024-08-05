#pragma once

class A {
  public:
    int *data;

    A(int *val) : data(val) {}

    int getValue() const {
        return *data; //
    }
};

#define crossFile(p) p->data = nullptr
#define bothInHeader() p->data = nullptr
#define bothInSource(x) x = nullptr
