#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <linux/input.h>

#define MOVE_ANGLE 0.04f
#define MOVE_POSITION 0.12f
#define RAYSTEP 0.02f
#define RAYSTEPS 5000
#define MAX_BALLS 64

typedef struct { float x, y, z; } Vec3;
typedef struct { float pitch, yaw; } Dir; /* radians */

static Vec3 vec3_add(Vec3 a, Vec3 b) {
	return (Vec3) {
		a.x+b.x, a.y+b.y, a.z+b.z
	};
}

static void vec3_add_ip(Vec3 *a, Vec3 b) {
	a->x+=b.x;
	a->y+=b.y;
	a->z+=b.z;
}

static Vec3 vec3_scale(Vec3 v, float s) {
	return (Vec3) {
		v.x*s, v.y*s, v.z*s
	};
}

static float vec3_dot(Vec3 a, Vec3 b) {
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

static float vec3_len(Vec3 v) {
	return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

static Vec3 vec3_norm(Vec3 v) {
	return vec3_scale(v, 1.0f / vec3_len(v));
}

static float vec3_dist(Vec3 a, Vec3 b) {
    return vec3_len(vec3_add(a, vec3_scale(b, -1.0f)));
}

static Vec3 dir_to_unit(Dir d) {
    return (Vec3) {
        cosf(d.pitch) * cosf(d.yaw),
        cosf(d.pitch) * sinf(d.yaw),
        sinf(d.pitch)
    };
}

// ball
typedef struct {
    Vec3 center;
    float radius;
} Ball;

static Vec3 ball_reflect(Ball b, Vec3 hit, Vec3 move) {
    Vec3 n = vec3_norm(vec3_add(hit, vec3_scale(b.center, -1.0f)));
    return vec3_add(move, vec3_scale(n, -2.0f * vec3_dot(n, move)));
}

// ray / rendering helpers
static int ray_hit_floor(Vec3 r) {
    return r.z <= 0.0f;
}

static char ray_to_char(Vec3 r, int reflections) {
    static const char shade[] = {'.', '-', ','};

    if(r.z <= 0.0f) {
        int tx = (int)floorf(r.x);
        int ty = (int)floorf(r.y);
        return (abs(tx - ty) % 2 == 0) ? '#' : ' ';
    }

    if(reflections <= 0) return ' ';
    if(reflections < 4) return shade[reflections - 1];
    return '+';
}

// input; evdev gives true simultaneous press/release for every key
typedef enum {
    K_W, K_A, K_S, K_D,
    K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_QUIT,
    K_COUNT
} GameKey;

/* one bit per GameKey; set on press, cleared on release */
static atomic_uint g_keys_held;

static void key_press(GameKey k) { atomic_fetch_or (&g_keys_held, 1u << k); }
static void key_release(GameKey k) { atomic_fetch_and(&g_keys_held, ~(1u << k)); }
static int  key_held(GameKey k) { return !!(atomic_load(&g_keys_held) & (1u << k)); }

static GameKey evcode_to_gamekey(int code) {
    switch (code) {
        case KEY_W: return K_W;
        case KEY_A: return K_A;
        case KEY_S: return K_S;
        case KEY_D: return K_D;
        case KEY_UP: return K_UP;
        case KEY_DOWN: return K_DOWN;
        case KEY_LEFT: return K_LEFT;
        case KEY_RIGHT: return K_RIGHT;
        case KEY_Q:
        case KEY_ESC: return K_QUIT;
        default: return K_COUNT;
    }
}

static void *input_thread(void *arg) {
    int fd = *(int *)arg;
    struct input_event ev;

    while(read(fd, &ev, sizeof ev) == sizeof ev) {
        if(ev.type != EV_KEY) continue;

        GameKey k = evcode_to_gamekey(ev.code);
        if(k == K_COUNT) continue;

        if(ev.value == 1) key_press(k); // key down
        else if (ev.value == 0) key_release(k); // key up
        //ev.value == 2; /* is auto-repeat; ignored, we track state ourselves */
    }

    return NULL;
}

// evdev terminal input
static struct termios g_saved_termios;

static void term_restore(void) {
    tcsetattr(STDOUT_FILENO, TCSAFLUSH, &g_saved_termios);
    printf("\033[?25h"); // show cursor
    printf("\033[2J"); // clear screen
    fflush(stdout);
}

static void term_init(void) {
    tcgetattr(STDOUT_FILENO, &g_saved_termios);
    atexit(term_restore);

    struct termios raw = g_saved_termios;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(unsigned)(IXON | ICRNL);
    tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw);

    printf("\033[?25l"); // hide cursor
    printf("\033[2J"); // clear screen
    fflush(stdout);
}

// scene
typedef struct {
    Ball balls[MAX_BALLS];
    int num_balls;
    Vec3 pos;
    Dir dir;
    float view_w;
    float view_h;
    int cols;
    int rows;
} Scene;

static void scene_init(Scene *s, Vec3 pos, Dir dir, float view_w, float view_h, int cols, int rows) {
    memset(s, 0, sizeof *s);
    s->pos = pos;
    s->dir = dir;
    s->view_w = view_w;
    s->view_h = view_h;
    s->cols = cols;
    s->rows = rows;
}

static void scene_add_ball(Scene *s, Ball b) {
    if(s->num_balls < MAX_BALLS) s->balls[s->num_balls++] = b;
}

static void scene_render(const Scene *s) {
    /* three basis vectors for the view rectangle:
     *   forward (v1) - center of screen
     *   up (v2) - toward top edge
     *   right (v3) - toward right edge
	*/
    Vec3 forward = dir_to_unit(s->dir);

    Vec3 up = vec3_scale((Vec3) {
        -tanf(s->dir.pitch) * forward.x,
        -tanf(s->dir.pitch) * forward.y,
         cosf(s->dir.pitch)
    }, s->view_h / 2.0f);

    Vec3 right = vec3_scale(vec3_norm((Vec3) { -forward.y, forward.x, 0.0f }), s->view_w / 2.0f);

    // write entire frame to a buffer, then flush once to minimise flicker
    int bufsize = 4 + s->rows * (s->cols + 2);
    char *buf = malloc(bufsize);
    int pos = 0;

    buf[pos++] = '\033';
    buf[pos++] = '[';
    buf[pos++] = 'H'; // cursor home

    float dists[MAX_BALLS];

    for(int row=0;row<s->rows;row++) {
        for(int col=0;col<s->cols;col++) {
            float v_offset = -((float)row/(s->rows-1)-0.5f);
            float h_offset = (float)col/(s->cols-1)-0.5f;

            Vec3 ray_dir = vec3_scale(
                vec3_norm(vec3_add(
                    vec3_add(forward, vec3_scale(up, v_offset)),
                    vec3_scale(right, h_offset)
                )),
                RAYSTEP
            );

            Vec3 ray = s->pos;
            int reflections = 0;

            for(int step=0;step<RAYSTEPS;step++) {
                if(ray_hit_floor(ray)) break;

                for(int bi=0;bi<s->num_balls;bi++) {
                    float d = vec3_dist(ray, s->balls[bi].center)-s->balls[bi].radius;
                    dists[bi] = d;
                    if(d<0.0f) {
                        ray_dir = ball_reflect(s->balls[bi], ray, ray_dir);
                        reflections++;
                    }
                }

                // sphere-trace: if we're far from everything, skip ahead
                float min_dist = ray.z;
                for(int bi=0;bi<s->num_balls; bi++) if(dists[bi] < min_dist) min_dist = dists[bi];

                if(min_dist>RAYSTEP) {
                    int skip = (int)(min_dist/RAYSTEP);
                    step += skip-1;
                    vec3_add_ip(&ray, vec3_scale(ray_dir, (float)skip));
                } else {
                    vec3_add_ip(&ray, ray_dir);
                }
            }

            buf[pos++] = ray_to_char(ray, reflections);
        }
        buf[pos++] = '\r';
        buf[pos++] = '\n';
    }

    fwrite(buf, 1, pos, stdout);
    fflush(stdout);
    free(buf);
}

static void scene_apply_input(Scene *s) {
    if(key_held(K_UP)) s->dir.pitch += MOVE_ANGLE;
    if(key_held(K_DOWN)) s->dir.pitch -= MOVE_ANGLE;
    if(key_held(K_LEFT)) s->dir.yaw -= MOVE_ANGLE;
    if(key_held(K_RIGHT)) s->dir.yaw += MOVE_ANGLE;

    if(s->dir.pitch>1.4f) s->dir.pitch = 1.4f;
    if(s->dir.pitch < -1.4f) s->dir.pitch = -1.4f;

    // move; project forward direction onto the XY plane
    if(!key_held(K_W) && !key_held(K_A) && !key_held(K_S) && !key_held(K_D)) return;

    Vec3 fwd = dir_to_unit(s->dir);
    float ground = sqrtf(fwd.x*fwd.x + fwd.y*fwd.y);
    if(ground<1e-6f) return;

    float fx = (fwd.x/ground)*MOVE_POSITION;
    float fy = (fwd.y/ground)*MOVE_POSITION;

    if(key_held(K_W)) { s->pos.x += fx; s->pos.y += fy; }
    if(key_held(K_S)) { s->pos.x -= fx; s->pos.y -= fy; }
    if(key_held(K_A)) { s->pos.x += fy; s->pos.y -= fx; }
    if(key_held(K_D)) { s->pos.x -= fy; s->pos.y += fx; }
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventN [cols rows]\n\n", argv[0], argv[0]);
        return 1;
    }

    const char *evdev_path = argv[1];
    int cols = (argc>=4) ? atoi(argv[2]) : 200;
    int rows = (argc>=4) ? atoi(argv[3]) : 100;

    int evfd = open(evdev_path, O_RDONLY);
    if(evfd<0) {
        perror(evdev_path);
        fprintf(stderr, "Try: sudo %s %s\n", argv[0], evdev_path);
        return 1;
    }

    atomic_store(&g_keys_held, 0u);
    term_init();

    pthread_t input_tid;
    pthread_create(&input_tid, NULL, input_thread, &evfd);
    pthread_detach(input_tid);

    Scene scene;
    scene_init(&scene, (Vec3){0.0f, 0.0f, 1.0f}, (Dir){-0.2f, 0.0f}, 2.0f, 2.0f, cols, rows);

    scene_add_ball(&scene, (Ball) {{ 5.0f,  0.0f, 2.0f}, 2.0f});
    scene_add_ball(&scene, (Ball) {{10.0f,  0.0f, 2.0f}, 2.0f});
    scene_add_ball(&scene, (Ball) {{ 7.5f,  0.0f, 8.0f}, 4.0f});

    while(!key_held(K_QUIT)) {
        scene_apply_input(&scene);
        scene_render(&scene);
    }

    close(evfd);
    return 0;
}
