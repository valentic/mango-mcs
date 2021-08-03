#!/usr/bin/env python

class DotDict(dict):
    """dot.notation access to dictionary attributes"""
    def __getattr__(*args):
        val = dict.__getitem__(*args)         
        return DotDict(val) if type(val) is dict else val      
    __setattr__ = dict.__setitem__     
    __delattr__ = dict.__delitem__


if __name__ == '__main__':
      
    defaults = dict(a=1,b=2)
    settings = dict(b=4)

    dd = DotDict(defaults, **settings)

    print(dd)
    print(dd.a)

