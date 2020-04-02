#pragma once

#include <string.h>

class String {
  public:
    String(const char *str, int length) {
        this->_length = length;
        this->_size = this->_length + 1;
        this->_string = new char[this->_size];
        this->_string[this->_length] = '\0';
        memcpy(this->_string, str, length);
    }

    String() : String("", 0) {
    }

    String(const char *str) : String(str, strlen(str)) {
    }

    String(const String &str) {
        this->_length = str._length;
        this->_size = str._size;
        this->_string = new char[this->_size];
        memcpy(this->_string, str._string, this->_size);
    }

    ~String() {
        if (this->_string != NULL) {
            delete[] this->_string;
            this->_string = NULL;
            this->_length = 0;
            this->_size = 0;
        }
    }

    String &operator=(const String &str) {
        if (this != &str) {
            this->_length = str._length;

            delete[] this->_string;
            this->_size = str._size;
            this->_string = new char[this->_size];

            memcpy(this->_string, str._string, str._length + 1);
        }

        return *this;
    }

    String &operator+=(const String &str) {
        const int oldLength = this->_length;
        this->_length = this->_length + str._length;
        this->_size = this->_length + 1;

        char *buffer = new char[this->_size];
        memcpy(buffer, this->_string, oldLength);
        memcpy(buffer + oldLength, str._string, str._length + 1);

        delete[] this->_string;
        this->_string = buffer;

        return *this;
    }

    inline const char *c_str() const {
        return this->_string;
    }

    inline char *c_str() {
        return this->_string;
    }

    inline int length() const {
        return this->_length;
    }

    inline int indexOf(char c) const {
        for (int i = 0; i < this->_length; ++i) {
            if (this->_string[i] == c) {
                return i;
            }
        }
        return -1;
    }

    String substring(int start, int end) const {
        return String(this->_string + start, end - start);
    }

    String substring(int start) const {
        return substring(start, this->_length);
    }

    inline bool isEmpty() const {
        return this->_length == 0;
    }

    friend String operator+(String a, String const &b);

  private:
    int _size;
    int _length;
    char *_string;
};