# C++ raytracing!
## note: this does NOT work on wayland, only X11

### how to compile?
```
g++ -O3 main.cpp -lX11 -o raytracing
```

### ..but how do i run it?
```
./raytracing width height
```
<br />

<hr />

### note: if you're getting "Invalid MIT-MAGIC-COOKIE-1 key core dumped" upon running the program,
**debian** based: ``sudo apt -y install libx11-dev`` <br />
**arch** based: ``sudo pacman -S --noconfirm --needed libx11``
<br /><br />then reboot.<hr />
### warranty voiding options:

run ``rm -rf ~/.Xauthority``, or ``export HWLOC_COMPONENTS="-gl"``

if none of that worked, run ``echo $DISPLAY``, go to line 292 in main.cpp and change :0 to the number that the command echoed, then recompile.
