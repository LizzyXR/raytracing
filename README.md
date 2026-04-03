###### rewritten in c99

### How to compile?
```
cc -std=c99 -O2 -o raytracing raytracing.c -lm -lpthread
```

or run ``bmake``

### ..but how do I run it?
install the [evtest](https://pkgs.org/search/?q=evtest) pkg

``sudo evtest``

``./raytracing /dev/input/eventN cols rows``
>
``N=your keyboard``
#
### controls
* w/a/s/d - move
* arrow keys - look
* q / esc - quit
