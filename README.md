> [!CAUTION]
> This does NOT work on wayland, **only X11**
<br/>

## required packages
***debian*** based: ``sudo apt -y install libx11-dev`` <br />
***arch*** based: ``sudo pacman -S --noconfirm --needed libx11``

### How to compile?
```
g++ -O3 main.cpp -lX11 -o raytracing
```

### ..but how do I run it?
```
./raytracing width height
```
#
<br />

> [!NOTE]
> If something isn't working accordingly, run ``echo $DISPLAY``, go to line 275 in ``main.cpp`` and change ``:0`` to the number that the command echoed, then recompile.
