#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <X11/Xlib.h>
#include "X11/keysym.h"
#include <string>
using namespace std;

// forward decleration for vect class
class Vect;

// helper functions
char ray_char(Vect *ray, int refl);
bool ray_done(Vect *ray);
string set_cursor(int row, int col);
bool key_is_pressed(KeySym ks);

// direction class prototype, directions are represented with spherical coordinates
class Direction {
	public:
		float ang_v;
		float ang_h;
		Vect to_unit();
};

// vect class full definition
class Vect {
	public:
		float x,y,z;

		void normalize() {
			float len = length();
			x /= len;
			y /= len;
			z /= len;
		}

		float length() {
			return sqrt(x*x+y*y+z*z);
		}

		void add(Vect v) {
			x += v.x;
			y += v.y;
			z += v.z;
		}

		void scale(float s) {
			x *= s;
			y *= s;
			z *= s;
		}

		Vect scaled(float s) {
			Vect v = {x*s,y*s,z*s};
			return v;
		}

		float dist(Vect other) {
			return sqrt((x-other.x)*(x-other.x)+(y-other.y)*(y-other.y)+(z-other.z)*(z-other.z));
		}

		float dot(Vect other) {
			return x*other.x+y*other.y+z*other.z;
		}

		Direction to_direction() {
			float ang_v = atan(z/(x*x+y*y));
			float ang_h = atan2(y,x);
			Direction dir = {ang_v,ang_h};
			return dir;
		}
};

// Direction class function declerations
inline Vect Direction::to_unit() {
	Vect v  = {cos(ang_v)*cos(ang_h),cos(ang_v)*sin(ang_h),sin(ang_v)};
	return v;
}

class Ball {
	public:
		Vect center;
		float radius;

		Vect reflect(Vect incoming, Vect move) {
			center.scale(-1);
			incoming.add(center);
			center.scale(-1);
			incoming.normalize();

			incoming.scale(-2*incoming.dot(move));
			Vect new_move = move;

			new_move.add(incoming);

			return new_move;
		}
};

#define MOVE_ANGLE 0.01
#define MOVE_POSITION 0.03

#define RAYSTEP 0.02
#define RAYSTEPS 5000

class Game {
	public:
		vector<Ball> balls;
		Vect pos;
		Direction dir;
		float width, height;
		int xres, yres;

		Game(Vect start_pos, Direction start_dir, float width, float height, int xres, int yres) {
			this->width = width;
			this->height = height;
			this->pos = start_pos;
			this->dir = start_dir;
			this->xres = xres;
			this->yres = yres;
		}

		void add_ball(Ball b) {
			balls.push_back(b);
		}

		void make_pic(void) {
			// rays through equidistant points on width*height rectangle with distance 1 from viewer
			Vect v1 = dir.to_unit();
			// v2 points from the middle of the rectangle to the upper edge
			Vect v2 = {
				-tan(dir.ang_v)*v1.x,
				-tan(dir.ang_v)*v1.y,
				cos(dir.ang_v)
			};

			v2.scale(height/2);
			
			// v3 points from the middle of the rectangle to the left edge
			Vect v3 = {-v1.y,v1.x,0};
			v3.normalize();
			v3.scale(width/2);

			for(int row=0;row<yres;++row) {
				for(int col=0;col<xres;++col) {
					float up_offset = -((float)row/(yres-1)-0.5);
					float left_offset = (float)col/(xres-1)-0.5;
					Vect move = v1;
					move.add(v2.scaled(up_offset));
					move.add(v3.scaled(left_offset));
					move.normalize();
					move.scale(RAYSTEP);

					Vect ray = pos;
					// trace ray
					vector<float> dists_to_balls;
					for(int i=0;i<balls.size();++i) dists_to_balls.push_back(0);

					int times_reflected = 0;
					for(int i=0;i<RAYSTEPS;++i) {
						if(ray_done(&ray)) break;

						int ball_index = 0;
						for(Ball b: balls) {
							float d = ray.dist(b.center)-b.radius;
							dists_to_balls[ball_index] = d;
							if(d<0) {
								move = b.reflect(ray,move);
								times_reflected++;
							}

							ball_index++;
						}

						// optimization: test if all distances are large enough to make
						// multiple steps at once
						float min_dist = ray.z;
						for(float f: dists_to_balls) if(f<min_dist) min_dist = f;

						if(min_dist>RAYSTEP) {
							int possible_steps = min_dist/RAYSTEP;
							i += possible_steps - 1; // -1 because of the default increment
							ray.add(move.scaled(possible_steps));
						} else ray.add(move);
					}

					cout << set_cursor(row,col) << ray_char(&ray,times_reflected) << flush;
				}
			}
		}

		void start(void) {
			KeySym keys[] = {XK_Up, XK_Down, XK_Left, XK_Right};

			while(true) {
				make_pic();
				for(int key: keys) {
					if(key_is_pressed(key)) {
						if(key_is_pressed(XK_Shift_L)) move_view(key);
						else move_position(key);
					}
				}
			}
		}

		void move_view(KeySym key) {
			if(key==XK_Up) dir.ang_v += MOVE_ANGLE;
			else if(key==XK_Down) dir.ang_v -= MOVE_ANGLE;
			else if(key==XK_Left) dir.ang_h -= MOVE_ANGLE;
			else if(key==XK_Right) dir.ang_h += MOVE_ANGLE;
		}

		void move_position(KeySym key) {
			Vect dir_vect = dir.to_unit();
			float xmov = dir_vect.x;
			float ymov = dir_vect.y;
			float scale = 1/sqrt(xmov*xmov+ymov*ymov);
			xmov *= scale;
			ymov *= scale;
			xmov *= MOVE_POSITION;
			ymov *= MOVE_POSITION;

			if(key==XK_Up) {
				pos.x += xmov;
				pos.y += ymov;
			} else if(key==XK_Down) {
				pos.x -= xmov;
				pos.y -= ymov;
			} else if(key==XK_Left) {
				pos.x += ymov;
				pos.y -= xmov;
			} else if(key==XK_Right) {
				pos.x -= ymov;
				pos.y += xmov;
			}
		}

		bool check_reflections(Vect *ray, Vect *move) {
			// check if the ray has to be reflected on one of the objects and change dir accordingly
			for(Ball ball: balls) {
				if(ray->dist(ball.center)<ball.radius) {
					*move = ball.reflect(*ray,*move);
					return true; // only 1 reflection
				}
			}

			return false;
		}
};

// for setting the position of the cursor in the terminal window
string set_cursor(int row, int col) {
	return "\033[" + to_string(row) + ";" + to_string(col) + "H";
}

// the ray ends when it hits the floor at z=0
bool ray_done(Vect *ray) {
	return ray->z <= 0;
}

// determines whether a character is to be printed for a finished ray
char ray_char(Vect *ray, int refl) {
	char chars[] = {'.', '-', ','};
	if(ray->z <= 0 && abs(((int)floor(ray->x))-((int)floor(ray->y)))%2==0) return '#';
	else if(refl>0) {
		if(refl<4) return chars[refl-1];
		else return '+';
	} else return ' ';
}

bool key_is_pressed(KeySym ks) {
	Display *dpy = XOpenDisplay(":0");
	char keys_return[32];
	XQueryKeymap(dpy, keys_return);
	KeyCode kc2 = XKeysymToKeycode(dpy, ks);
	bool isPressed = !!(keys_return[kc2>>3]&(1<<(kc2&7)));
	XCloseDisplay(dpy);

	return isPressed;
}

int main(int argc, char *argv[]) {
	Vect start_pos = {0,0,1};
	Direction start_dir = {-0.2,0};

	int width, height;
	if(argc==1) {
		// if no window size is given, set the default to 100x200
		width = 200;
		height = 100;
	} else {
		width = stoi(argv[1]);
		height = stoi(argv[2]);
	}

	Game game = Game(start_pos, start_dir, 2, 2, width, height);
	// add some bawlz
	Ball b = {{5,0,2},2};
	game.add_ball(b);

	Ball c = {{10,0,2},2};
	game.add_ball(c);

	Ball d = {{7.5,0,8}, 4};
	game.add_ball(d);

	game.start();
}
