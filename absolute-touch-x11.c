#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <linux/input.h>
#include <xdo.h>

// ---
#define ROLL_OFFSET		50	// offset relative to display width, 1920
#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

/* Display number to use in xdo functions*/
#define DISPLAY_NUM 0
/* Disable to control the area on the screen where the cursor is moved around (using geometry_x,y,w,h)*/
#define MAP_TO_ENTIRE_SCREEN 1
/* if disabled, the device file will not be grabbed for exclusive access, and will not handle movement events*/
#define HANDLE_MOVEMENTS 1

#define AX_X 0
#define AX_Y 1

enum touch_status{
   TOUCH_DOWN_NONE,     // no contact
   TOUCH_DOWN_NEW,      // new contact
   TOUCH_DOWN_CONT,     // continued contact
   TOUCH_DOWN_MULTI,    // multi-touch (2 fingers)
} touch_down = TOUCH_DOWN_NONE;

enum absval{
   ABSVAL_VALUE,
   ABSVAL_MIN,
   ABSVAL_MAX,
   ABSVAL_FUZZ,
   ABSVAL_FLAT,
   ABSVAL_RESOLUTION
};

struct touchpad_device_absdata{
   int min_value_abs_x;
   int max_value_abs_x;
   int min_value_abs_y;
   int max_value_abs_y;
} device_absdata;

struct input_event ev;
int fd;
char * device_file_path;
// --- touch variables
int touch_x = -1;
int touch_y = -1;
int initial_x = -1;
int initial_y = -1;
// geometry of area to map to on display
int geometry_x = 100;
int geometry_y = 100;
int geometry_w = 780;
int geometry_h = 320;
//
int SCREEN_WIDTH = 1920;
int SCREEN_HEIGHT	= 1080;

xdo_t * xdo_tool;

volatile sig_atomic_t stop = 0;

void interrupt_handler(int sig)
{
	stop = 1;
}

int is_event_device(const struct dirent *dir) {
	return strncmp(EVENT_DEV_NAME, dir->d_name, 5) == 0;
}
/**
 * Scans all /dev/input/event*, display them and ask the user which one to
 * open.
 *
 * @param device_file_path string variable to write result file path to
 */
int scan_devices(char ** device_file_path)
{
	struct dirent **namelist;
	int  i,ndev, devnum;
	char *filename;
	//int max_device = 0;

	ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, alphasort);
	if (ndev <= 0)
		return 1;

	fprintf(stderr, "Available devices:\n");

	for (i = 0; i < ndev; i++)
	{
		char fname[64];
		int fd = -1;
		char name[256] = "???";

		snprintf(fname, sizeof(fname),
			 "%s/%s", DEV_INPUT_EVENT, namelist[i]->d_name);
		fd = open(fname, O_RDONLY);
		if (fd < 0)
			continue;
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);

		fprintf(stderr, "%s:	%s\n", fname, name);
		close(fd);

		sscanf(namelist[i]->d_name, "event%d", &devnum);
		//if (devnum > max_device)
		//	max_device = devnum;

		free(namelist[i]);

      if (strstr(name, "TouchPad") != NULL){
         *device_file_path = (char *) malloc(64);
         strcpy(*device_file_path, fname);
         return 0;
      }
	}

	fprintf(stderr, "Select the device event number: ");
	scanf("%d", &devnum);

	sprintf(filename, "%s/%s%d",
		 DEV_INPUT_EVENT, EVENT_DEV_NAME,
		 devnum);

	return 0;
}

/**
 * Record additional information for absolute axes (min/max value).
 *
 * @param fd The file descriptor to the device.
 * @param axis The axis identifier (e.g. ABS_X).
 */
void record_absdata(int fd, struct touchpad_device_absdata * device_absdata)
{
	int abs_x[6], abs_y[6] = {0};
	int k;

	ioctl(fd, EVIOCGABS(AX_X), abs_x);
	ioctl(fd, EVIOCGABS(AX_Y), abs_y);
	for (k = 0; k < 6; k++){
		if ((k < 3) || abs_x[k] || abs_y[k]){
			//printf("      %s %6d\n", absval[k], abs_x[k]);
         if (k == ABSVAL_MIN){
            device_absdata -> min_value_abs_x = abs_x[k];
            device_absdata -> min_value_abs_y = abs_y[k];
         }
         else if (k == ABSVAL_MAX){
            device_absdata -> max_value_abs_x = abs_x[k];
            device_absdata -> max_value_abs_y = abs_y[k];
         }
      }
   }

   fprintf(stderr, "min x = %d\n", device_absdata -> min_value_abs_x);
   fprintf(stderr, "max x = %d\n", device_absdata -> max_value_abs_x);
   fprintf(stderr, "min y = %d\n", device_absdata -> min_value_abs_y);
   fprintf(stderr, "max y = %d\n", device_absdata -> max_value_abs_y);
}

/**
 * Open device, grabs it for exclusive access if HANDLE_MOVEMENTS is set to 1, 
 * then record some data required later
 */
int init_dev_event_reader(){
   char name[256] = "???";
	if ((getuid ()) != 0) {
        fprintf(stderr, "You are not root! This may not work...\n");
        return 1;
    }

    /* Open Device */
    fd = open(device_file_path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "%s is not a vaild device\n", device_file_path);
        return 1;
    }

    /* Print Device Name */
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    printf("Reading from:\n");
    printf("device file = %s\n", device_file_path);
    printf("device name = %s\n", name);

   if(HANDLE_MOVEMENTS)
	   ioctl(fd, EVIOCGRAB, (void*)1);

   record_absdata(fd, &device_absdata);

   return 0;
}

/**
 * Translate x or y coordinates on touchpad to coordinates on display														|
 *
 * @param point x or y coordinate from ev.value
 * @param type 0 for x, 1 for y
 */
int translate_pt(
	int point,	// x/y coordinate from ev.value
	bool type		// 0 for x, 1 for y
	)
{
	int display_offset, display_size, touchpad_min, touchpad_size;
	if(!type){ //x
		display_offset = geometry_x;
		display_size = geometry_w;
		touchpad_min = device_absdata.min_value_abs_x;
		touchpad_size = device_absdata.max_value_abs_x - device_absdata.min_value_abs_x;
	}
	else{ //y
		display_offset = geometry_y;
		display_size = geometry_h;
		touchpad_min = device_absdata.min_value_abs_y;
		touchpad_size = device_absdata.max_value_abs_y - device_absdata.min_value_abs_y;
	}
	point = point - touchpad_min;
	return display_offset + (int) floor(point*display_size/touchpad_size);
}

/**
 * Change geometry_x,y values
 *
 * @param rel_x distance to move geometry in the x axis direction
 * @param rel_y distance to move geometry in the y axis direction
 */
int move_geometry(int rel_x, int rel_y){

	geometry_x += rel_x;
	geometry_y += rel_y;
	if (geometry_x < 0) geometry_x=0;
	if (geometry_y < 0) geometry_y=0;
	if (geometry_x > SCREEN_WIDTH-geometry_w) geometry_x=SCREEN_WIDTH-geometry_w;
	if (geometry_y > SCREEN_HEIGHT-geometry_h) geometry_y=SCREEN_HEIGHT-geometry_h;

	printf("geometry changed to: %d, %d \n", geometry_x, geometry_y);
	return 0;
}

/**
 * Move mouse using xdo library
 * 
 * @param x position of finger on touchpad
 * @param y position of finger on touchpad
 */
int mousemove(int x, int y){
   return xdo_move_mouse(
      xdo_tool, 
      translate_pt(x, 0), 
      translate_pt(y, 1), 
      DISPLAY_NUM
      );
}

/**
 * Hold down/ Release mouse button
 *
 * @param down hold down mouse button if true, otherwise release it
 */
int mousebtn(bool down){
   if (down)
      return xdo_mouse_down(xdo_tool, CURRENTWINDOW, 1);
   else
      return xdo_mouse_up(xdo_tool, CURRENTWINDOW, 1);
}

/**
 * function to easily move geometry_x to the left/right
 */
void roll(bool go_right){
	int rel_x = (go_right? 1:-1) * (geometry_w - ROLL_OFFSET);
	move_geometry(rel_x, 0);
	return;
}

int event_listener_loop(){
	const size_t ev_size = sizeof(struct input_event);
	ssize_t size;
   int i;
	fd_set rdfs;

	FD_ZERO(&rdfs);
	FD_SET(fd, &rdfs);

   while (!stop){
      select(fd + 1, &rdfs, NULL, NULL, NULL);
      if (stop)
         break;
      size = read(fd, &ev, ev_size);

      if (size < ev_size) {
        fprintf(stderr, "Error size when reading\n");
        return 1;
      }

      for (i = 0; i < size / ev_size; i++){
         switch(ev.type)
         {
            case EV_ABS:
               switch(touch_down)
               {
                  case TOUCH_DOWN_NONE:
                     if (ev.code == ABS_MT_TRACKING_ID && ev.value != -1) {
                        touch_down = TOUCH_DOWN_NEW;
                     }
                     break;

                  case TOUCH_DOWN_NEW:
                     if (ev.code == ABS_X)
                        initial_x = touch_x = ev.value;

                     else if (ev.code == ABS_Y)
                        initial_y = touch_y = ev.value;

                     if (touch_x != -1 && touch_y != -1){
                        mousemove(touch_x, touch_y);
                        mousebtn(1);
                        touch_x=touch_y=-1;
                        touch_down = TOUCH_DOWN_CONT;
                     }
                     break;

                  case TOUCH_DOWN_CONT:
                     if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1){
                        mousebtn(0);
                        touch_x = touch_y = -1;
                        touch_down = TOUCH_DOWN_NONE;
                     }

                     else if (ev.code == ABS_X)
                        touch_x = ev.value;
                     else if (ev.code ==ABS_Y)
                        touch_y = ev.value;

                     if (HANDLE_MOVEMENTS && touch_x != -1 && touch_y != -1){
                        mousemove(touch_x, touch_y);
                        mousebtn(1);
                        touch_x=touch_y=-1;
                     }
                     break;

                  case TOUCH_DOWN_MULTI:
                     if (ev.code == ABS_X)
                        touch_x = ev.value;
                     else if (ev.code == ABS_Y)
                        touch_y = ev.value;

                     if (touch_x != -1 && touch_y != -1){
                        int rel_x = touch_x - initial_x;
                        int rel_y = touch_y - initial_y;
                        int touchpad_size = device_absdata.max_value_abs_x - device_absdata.min_value_abs_x;
                        rel_x = (int) floor(SCREEN_WIDTH*rel_x/touchpad_size);
                        touchpad_size = device_absdata.max_value_abs_y - device_absdata.min_value_abs_y;
                        rel_y = (int) floor(SCREEN_WIDTH*rel_y/touchpad_size);
                        move_geometry(rel_x, rel_y);

                        initial_x = touch_x; initial_y = touch_y;
                        touch_x = touch_y = -1;
                     }
                     if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1){
                        touch_down = TOUCH_DOWN_NONE;
                        }
                     break;

               }
               break;

            case EV_KEY:
               if (ev.code == BTN_TOOL_DOUBLETAP && ev.value == 1)
               {
                  mousebtn(0);
                  touch_down = TOUCH_DOWN_MULTI;
               }

               if (ev.code == BTN_LEFT && ev.value==1){
                  printf("cpa");
                  roll(0);
               }
               if (ev.code == BTN_RIGHT && ev.value==1){
                  printf("cpa");
                  roll(1);
               }

               break;
         }
      }
   }
	return 0;
}


int main(int argc, char** argv)
{
   if (argc > 1){
      device_file_path = (char *) malloc(64);
      sprintf(device_file_path, "%s/%s%s", DEV_INPUT_EVENT, EVENT_DEV_NAME, argv[1]);
   }

   else{
      if (scan_devices(&device_file_path))
         return EXIT_FAILURE;
   }

   xdo_tool = xdo_new(NULL);
   if (xdo_tool == NULL){
      fprintf(stderr, "failed at xdo_new()");
      return EXIT_FAILURE;
   }

   xdo_get_viewport_dimensions(xdo_tool, &SCREEN_WIDTH, &SCREEN_HEIGHT, DISPLAY_NUM);
   fprintf(stderr, "display dimensions: %d x %d\n", SCREEN_WIDTH, SCREEN_HEIGHT);

   if (MAP_TO_ENTIRE_SCREEN){
      geometry_x = geometry_y = 0;
      geometry_w = SCREEN_WIDTH;
      geometry_h = SCREEN_HEIGHT;
   }

	int ret = init_dev_event_reader();
	if (ret == 1)
		return EXIT_FAILURE;

	signal(SIGINT, interrupt_handler);
	signal(SIGTERM, interrupt_handler);

   event_listener_loop();

   close(fd);
	ioctl(fd, EVIOCGRAB, (void*)0);

   return EXIT_SUCCESS;
}
