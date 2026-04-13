#include <SDL2/SDL_keycode.h>
#include <unistd.h>
#include <time.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_render.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include <glib.h>
#include <json-c/json.h>
#include <png.h>
#include <wchar.h>
#include "local_resolve.h"

#define SDL_MAIN_HANDLED

#define TARGET_LOOP_DURATION_SECONDS 0.0333

pthread_mutex_t lock;

union pixel {
  unsigned int i;
  unsigned char c[4];
};

typedef struct {
  unsigned char* data;
  size_t size;
} memory_buffer;

typedef struct {
  unsigned int* image;
  int width;
  int height;
  char* hostname;
  char* port;

  int active;
  int kill;

  char* decoded_message;
} llm_thread_data;

static inline int position_inside(int width,
				  int height,
				  int x, int y) {
  return((x >= 0 && x < width && y >= 0 && y < height));
}

int load_png_image(FILE* f, unsigned int** out_image,
		   unsigned int* width,
		   unsigned int* height) {

  int i,j;

  png_byte color_type, depth;
  png_bytep *row = NULL;
  png_bytep png_pixel;
  
  png_infop info;
  
  png_structp structure = NULL;

  unsigned int* pixel_i;
  unsigned char* pixel_c;

  unsigned int * image;
  
  structure = png_create_read_struct(PNG_LIBPNG_VER_STRING,
				     NULL, NULL, NULL);
  if(!structure) return(1);

  info = png_create_info_struct(structure);

  if(!info) return(2);

  png_init_io(structure, f);
  png_read_info(structure, info);

  width[0] = png_get_image_width(structure, info);
  height[0] = png_get_image_height(structure, info);
  color_type = png_get_color_type(structure, info);
  depth = png_get_bit_depth(structure, info);

  if (depth == 16) {
    png_set_strip_16(structure);
  }

  
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(structure);
  }

  if (color_type == PNG_COLOR_TYPE_GRAY && depth < 8) {
    png_set_expand_gray_1_2_4_to_8(structure);
  }

  if (png_get_valid(structure, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(structure);
  }

  if (color_type == PNG_COLOR_TYPE_RGB ||
      color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_PALETTE) {

    png_set_filler(structure, 0xFF, PNG_FILLER_AFTER);
  }

  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(structure);
  }

  png_read_update_info(structure,info);

  row = (png_bytep*)malloc(sizeof(png_bytep)*height[0]);
  
  if(row == NULL) return 3;
  
  for(i = 0; i < height[0]; i++) {
    row[i] = (png_byte*)malloc((size_t)png_get_rowbytes(structure,info));
    if(row[i] == NULL) return 3;
  }

  image = (unsigned int*)malloc(sizeof(unsigned int)*width[0]*height[0]);

  if(image == NULL) return 3;
  
  png_read_image(structure,row);

  for(j = 0; j < height[0]; j++) {
    for(i = 0; i < width[0]; i++) {
      png_pixel = (row[j]+(i*4));
      pixel_i = image+j*width[0]+i;
      pixel_c = (unsigned char*)pixel_i;
      
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      
      pixel_c[0] = png_pixel[2];
      pixel_c[1] = png_pixel[1];
      pixel_c[2] = png_pixel[0];
      pixel_c[3] = png_pixel[3];
      
#endif
      
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      
      pixel_c[3] = png_pixel[2];
      pixel_c[2] = png_pixel[1];
      pixel_c[1] = png_pixel[0];
      pixel_c[0] = png_pixel[3];
      
#endif
    }
  }
  
 out_image[0] = image;
 
  for(i = 0; i < height[0]; i++) {
    free(row[i]);
  }
  free(row);
  
  return(0);
}

void image_file_read_error(char* filename) {
  printf("Fatal Error: Could not reliable read the PNG file %s.\n", filename);
  _exit(1);
}

void usage(char* program_name) {
  printf("OCR Tool, that works together with a llama.cpp like server\n"
	 "and an OCR model. The tool yields the selected text on stdout.\n"
	 "Usage:\n"
	 "  %s [image-file.png] [ipaddress] [port] \n"
	 "where image-file.png is a png file. ipaddress and port\n"
	 "are the ip address and the port of a server running an OCR model\n",
	 program_name);
  _exit(1);
}

void image_to_frame_with_zoom_at_point(int image_start_x, int image_start_y,
				       int zoom,
				       int image_width, int image_height,
				       int screen_width, int screen_height,
				       unsigned int* render_frame,
				       unsigned int* image) {

  int i, j, k, l;

  int image_end_x, image_end_y;

  int pos_x_in_render_frame;
  int pos_y_in_render_frame;
 
  int pos_x_in_image;
  int pos_y_in_image;


  
  image_end_y = image_start_y + screen_height / zoom - screen_height%zoom;
  if (image_end_y > image_height) {
    image_end_y = image_height;
  }
  
  image_end_x = image_start_x + screen_width / zoom - screen_width%zoom;
  if (image_end_x > image_width) {
    image_end_x = image_width;
  }
    
  for(i=image_start_x; i<image_end_x; i++) {
    pos_x_in_image = i;
    pos_x_in_render_frame = (i-image_start_x)*zoom;
    for(j=image_start_y; j<image_end_y; j++) {
      pos_y_in_image = j;
      pos_y_in_render_frame = (j-image_start_y)*zoom;
      for(l=0;l<zoom;l++) {
	for(k=0;k<zoom;k++) {
	  
          render_frame[(pos_y_in_render_frame+l)*screen_width
		       +pos_x_in_render_frame+k] =
	    image[pos_y_in_image*image_width
		  +pos_x_in_image];

	}
      }
    }
  }
}

static inline void draw_rect(unsigned int* frame, int start_x, int start_y,
			     int end_x, int end_y, int width, int height) {


  int i;
  int s_x, e_x;
  int s_y, e_y;

  union pixel red;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  red.c[0] = 0;
  red.c[1] = 0;
  red.c[2] = 255;
  red.c[3] = 255;
#else
  red.c[3] = 0;
  red.c[2] = 0;
  red.c[1] = 255;
  red.c[0] = 255;
#endif
  
  if(start_x > end_x) {
    s_x = end_x; e_x = start_x;
  } else {
    s_x = start_x; e_x = end_x;
  }

  if(start_y > end_y) {
    s_y = end_y; e_y = start_y;
  } else {
    s_y = start_y; e_y = end_y;
  }

  for(i = s_x; i<e_x;i++) {
    frame[width*s_y+i] = red.i;
    frame[width*e_y+i] = red.i;
  }
  for(i = s_y;i<e_y;i++) {
    frame[width*i+s_x] = red.i;
    frame[width*i+e_x] = red.i;
  }
}

static inline void inverter_check(int* start, int* end) {

  int buffer;
  
  if(start[0] > end[0]) {
    buffer = start[0];
    start[0] = end[0];
    end[0] = buffer;
  }
}
    
unsigned int* extract_image(unsigned int *huge_image, int huge_width,
                            int huge_height, int start_x, int start_y,
                            int end_x, int end_y, int zoom,
			    int *size_x, int *size_y) {
  int width, height;
  unsigned int* image;

  int i,j;
  
  inverter_check(&start_x, &end_x);
  inverter_check(&start_y, &end_y);

  width = end_x - start_x;
  height = end_y - start_y;
  
  image =
    (unsigned int*)malloc(sizeof(unsigned int)*width*height);

  for(j=0;j<height;j++) {
    for(i=0;i<width;i++) {
      image[j*width+i] = huge_image[(j+start_y)*huge_width+(i+start_x)];
    }
  }

  size_x[0] = width;
  size_y[0] = height;
  
  return(image);
  
}

void png_to_memory_callback(png_structp png_ptr,
			    png_bytep data,
			    png_size_t length) {

  memory_buffer * buffer = (memory_buffer*)png_get_io_ptr(png_ptr);
  buffer->data = realloc(buffer->data, buffer->size + length);
  memcpy(buffer->data + buffer->size, data, length);
  buffer->size += length;
}

static int kill_curl_progress_callback(void* thread_data,
				       curl_off_t total_down,
				       curl_off_t down_now,
				       curl_off_t total_up,
				       curl_off_t up_now) {

  llm_thread_data* data = (llm_thread_data*)thread_data;
  int kill_now;
  pthread_mutex_lock(&lock);
  kill_now = data->kill;
  pthread_mutex_unlock(&lock);  
  if(kill_now) {
    return 1;
  }
  return 0;
}

char * image_to_png_base_sixtyfour(unsigned int* image,
				   int width, int height){
  
  int i,j;
  
  png_bytep *row = NULL;
  png_bytep png_pixel;

  unsigned int* pixel_i;
  unsigned char* pixel_c;
  
  
  memory_buffer png_storage;
  png_structp structure;
  png_infop info;

  gchar* g_base64_encoded;
  size_t base64_length;
  char* base64_encoded;
  
  memset(&png_storage,0,sizeof(png_storage));

  structure = png_create_write_struct(PNG_LIBPNG_VER_STRING,
						  NULL,NULL,NULL);

  info = png_create_info_struct(structure);

  png_set_write_fn(structure, &png_storage, png_to_memory_callback, NULL);

  png_set_IHDR(structure, info, width, height, 8,
	       PNG_COLOR_TYPE_RGBA,
	       PNG_INTERLACE_NONE,
	       PNG_COMPRESSION_TYPE_DEFAULT,
	       PNG_FILTER_TYPE_DEFAULT);

   png_write_info(structure, info);

   row = (png_bytep*)malloc(sizeof(png_bytep)*height);

   for(i = 0; i < height; i++) {
    row[i] = (png_byte*)malloc(sizeof(unsigned int)*width);
   }

   for(j = 0; j < height; j++) {
     for(i = 0; i < width; i++) {
       png_pixel = (row[j]+(i*4));
       pixel_i = image+j*width+i;
       pixel_c = (unsigned char*)pixel_i;
       
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
       
       png_pixel[2] = pixel_c[0];
       png_pixel[1] = pixel_c[1];
       png_pixel[0] = pixel_c[2];
       png_pixel[3] = pixel_c[3];
       
#endif
       
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
       
       png_pixel[2] = pixel_c[3];
       png_pixel[1] = pixel_c[2];
       png_pixel[0] = pixel_c[1];
       png_pixel[3] = pixel_c[0];
       
#endif
     }
   }
   png_write_image(structure, row);
   png_write_end(structure, NULL);

   for(i = 0; i < height; i++) {
     free(row[i]);
   }
   free(row);
   
   png_destroy_write_struct(&structure, &info);

   g_base64_encoded = g_base64_encode(png_storage.data,png_storage.size);

   base64_length = strlen(g_base64_encoded);
   base64_encoded = (char*)malloc(base64_length+1);
  
   memcpy(base64_encoded,g_base64_encoded,base64_length);
   base64_encoded[base64_length] = 0;
   g_free(g_base64_encoded);
   free(png_storage.data);
   return(base64_encoded);
}

size_t write_function_callback(char* in_data,
			       size_t size,
			       size_t nmemb,
			       void* clientdata) {

  size_t totalsize = size * nmemb;
  memory_buffer * data = (memory_buffer*)clientdata;

  data->data = (unsigned char*)realloc(data->data , data->size + totalsize + 1);
  if(!data->data) {
    return 0;
  }

  memcpy(data->data+(data->size),in_data, totalsize);
  data->size += totalsize;
  data->data[data->size] = 0;

  return(totalsize);
}

char* decode_data(memory_buffer data) {

  char *json_str = (char*)data.data;
  const char *content_buffer;
  size_t content_length;
  char *content = NULL;

  struct json_object *root = json_tokener_parse(json_str);

  struct json_object *choices_array = NULL;
  struct json_object *first_choice = NULL;
  struct json_object *message_obj = NULL;
  struct json_object *content_obj = NULL;

  if (json_object_object_get_ex(root, "choices", &choices_array)) {

    first_choice = json_object_array_get_idx(choices_array, 0);
    
    if (first_choice) {
      
      if (json_object_object_get_ex(first_choice, "message", &message_obj)) {
	
	if (json_object_object_get_ex(message_obj, "content", &content_obj)) {
	  
	  content_buffer = json_object_get_string(content_obj);
	  content_length = strlen(content_buffer);
	  content = (char*)malloc(sizeof(char)*(content_length+1));
	  memcpy(content,content_buffer,content_length);
	  content[content_length] = 0;
	  	  
	} else {
	  goto fail_to_decode;
	}
      } else {
	goto fail_to_decode;
      }
    } else {
      goto fail_to_decode;
    }
  } else {
    goto fail_to_decode;
  }

  json_object_put(root);
  return content;

 fail_to_decode:
  return NULL;
}

void invoke_model(unsigned int* image,
		  int width,
		  int height,
		  char* hostname, char* port, llm_thread_data* thread_data) {

  char json_head[] =
    "{\"model\":\"model\",\"messages\":[{\"role\":\"user\","
    "\"content\":[{\"type\":\"image_url\",\"image_url\":{"
    "\"url\":\"data:image/png;base64,";

  char json_foot[] =
    "\"}},{\"type\":\"text\",\"text\":\"Text Recognition:\"}]}],"
    "\"temperature\":0.02}";

  char * base;
  char * full_json;
  size_t full_json_size;

  CURL* curl = curl_easy_init();

  struct curl_slist *host = NULL;
  struct curl_slist *headers = NULL;
  char* curl_slist_string = NULL;
  size_t curl_slist_string_size;

  char * ip_address;

  CURLcode result;

  memory_buffer rd;

  int terminate = 0;

  char* url;
  size_t url_size;

  char* json_decoded_string;
  
  base = image_to_png_base_sixtyfour(image,width,height);

  /*
  fprintf(stdout,"base64:\n%s",base);
  */
  
  full_json_size = strlen(json_head)+strlen(json_foot)+strlen(base);
  
  full_json = (char*)malloc(sizeof(char)*(full_json_size+1));
  full_json[0] = 0;

  strcat(full_json,json_head);
  strcat(full_json,base);
  strcat(full_json,json_foot);

  /*
  fprintf(stdout,"full json:\n%s",full_json);
  */
  
  url_size = strlen(hostname)+strlen(port)+30;
  url = (char*)malloc(sizeof(char)*url_size);
  sprintf(url,"http://%s:%s/v1/chat/completions",hostname,port);

  rd.size = 0;
  rd.data = NULL;

  if(!curl) {
    fprintf(stderr,"Failed to initialize CURL, EXITING\n");
    _exit(1);
  }

  ip_address = local_resolve(hostname);

  if(ip_address != NULL) {

    curl_slist_string_size = strlen(hostname)
      + strlen(port) + strlen(ip_address) + 3;
    
    curl_slist_string = (char*)malloc(sizeof(char)*curl_slist_string_size);
    sprintf(curl_slist_string,"%s:%s:%s",hostname,port,ip_address);

    host = curl_slist_append(NULL,curl_slist_string);

    curl_easy_setopt(curl, CURLOPT_RESOLVE,host);
  }

  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  
  curl_easy_setopt(curl, CURLOPT_URL,url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(full_json));
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, full_json);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&rd);

  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, kill_curl_progress_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, thread_data);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  
  result = curl_easy_perform(curl);

  if(result == CURLE_OK) {
  
    json_decoded_string = decode_data(rd);
    if(json_decoded_string != NULL) {
      fprintf(stdout,"%s",json_decoded_string);
      fflush(stdout);
    } else {
      fprintf(stderr,
	      "Error: Failed to Decode, Server returned the following:\n %s",
	      rd.data);
      fflush(stderr);
    }
    free(json_decoded_string);
  }
  free(full_json);
  free(url);

  if(host != NULL) curl_slist_free_all(host);
  if(headers != NULL) curl_slist_free_all(headers);
  
  if(curl_slist_string != NULL) free(curl_slist_string);
  if(ip_address != NULL) free(ip_address);
  if(rd.data != NULL) free(rd.data);
  curl_easy_cleanup(curl); 
}

void* invoke_model_thread(void* arg) {
  llm_thread_data* td = (llm_thread_data*)arg;

  invoke_model(td->image,
	       td->width,
	       td->height,
	       td->hostname,
	       td->port,
	       td);
  pthread_mutex_lock(&lock);
  td->active = 0;
  pthread_mutex_unlock(&lock);
  free(td->image);
  return NULL;
}

void graceful_exit(llm_thread_data* thread_data,struct timespec quit_delay) {

  int currently_active;
  
  pthread_mutex_lock(&lock);
  currently_active = thread_data->active;
  pthread_mutex_unlock(&lock);
  if (thread_data != 0 && currently_active) {
    fprintf(stderr,"Request Cancelled!\n");
    pthread_mutex_lock(&lock);
    thread_data->kill = 1;
    pthread_mutex_unlock(&lock);
  }	  /* wait for thread to quit before exiting */
  while (currently_active) {
    pthread_mutex_lock(&lock);
    currently_active = thread_data->active;
    pthread_mutex_unlock(&lock);
    nanosleep(&quit_delay,NULL);
  }
  return;
}

static inline void redraw(SDL_Renderer* renderer, SDL_Texture* texture,
			  unsigned int* image, int pitch,
			  int screen_width, int screen_height,
			  int image_origin_x, int image_origin_y,
			  int image_width, int image_height,
			  int zoom) {
  
  unsigned int* image_frame;
  
  SDL_LockTexture(texture, NULL, (void**)&image_frame, &pitch);
  memset(image_frame,0,sizeof(int)*screen_width*screen_height);
  image_to_frame_with_zoom_at_point(image_origin_x, image_origin_y,
				    zoom,
				    image_width, image_height,
				    screen_width, screen_height,
				    image_frame,
				    image);
  SDL_UnlockTexture(texture);
  SDL_RenderCopy(renderer,texture,NULL,NULL);

}

int main(int argc, char** argv) {

  char* png_file_name = argv[1];
  char* hostname = argv[2];
  char* port = argv[3];
    
  unsigned int* image;

  unsigned int screen_width=1600, screen_height=1000;
  int pitch = 4*screen_width;
  
  unsigned int image_width, image_height;
  
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  SDL_Texture *texture = NULL;

  SDL_Event event;

  unsigned int* image_frame;
  unsigned int* out_image;
  
  FILE* image_file;

  int rect_start_x;
  int rect_start_y;
  int rect_mouse_x;
  int rect_mouse_y;

  int image_out_sizex;
  int image_out_sizey;

  
  int shift_start_x;
  int shift_start_y;
  int shift_mouse_x;
  int shift_mouse_y;
  
  int image_origin_x = 0;
  int image_origin_y = 0;

  int zoom = 1;
  
  int mouse_left_down = 0;
  int mouse_middle_down = 0;
  
  unsigned int* off_screen_frame;
  int off_screen_frame_available = 0;

  struct timespec start, end,loop_delay, quit_delay;
  long loop_duration;

  union pixel black_opaque;

  llm_thread_data* thread_data = NULL;
  pthread_t thread;
  int currently_active;

  thread_data = (llm_thread_data*)malloc(sizeof(llm_thread_data));
  thread_data->active = 0;
  
  loop_delay.tv_sec = 0;
  loop_delay.tv_nsec = 0;

  quit_delay.tv_sec = 0;
  quit_delay.tv_nsec = 10000000; /* 10 milliseconds */
  
  pthread_mutex_init(&lock, NULL);
  
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  black_opaque.c[0] = 0;
  black_opaque.c[1] = 0;
  black_opaque.c[2] = 0;
  black_opaque.c[3] = 255;
#else
  black_opaque.c[3] = 0;
  black_opaque.c[2] = 0;
  black_opaque.c[1] = 0;
  black_opaque.c[0] = 255;
#endif
  
  if(argc > 1) {
    if (access(png_file_name, F_OK) == 0) {
      if (access(png_file_name, R_OK) == 0) {

	image_file = fopen(png_file_name,"rb");
	if (3 == load_png_image(image_file,
				&image,
				&image_width,
				&image_height)) {
	  image_file_read_error(png_file_name);
	} else {
	  fclose(image_file);
	}
      } else {
	image_file_read_error(png_file_name);
      }
    } else {
      image_file_read_error(png_file_name);
    }
  } else {
    usage(argv[0]);
  }
  
  SDL_SetMainReady();

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  window = SDL_CreateWindow("AI Enabled OCR Tool", 100, 20,
				  screen_width, screen_height,0);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if(renderer == NULL) {
    fprintf(stderr,
	    "Failed to initialize a hardware accelerated renderer,\n"
	    "falling back to software. \n");
    renderer = SDL_CreateRenderer(window,-1, SDL_RENDERER_SOFTWARE);
  }

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
			      SDL_TEXTUREACCESS_STREAMING,
			      screen_width, screen_height);

  SDL_LockTexture(texture, NULL, (void**)&image_frame, &pitch);

  memset(image_frame,0,sizeof(int)*screen_width*screen_height);
  
  image_to_frame_with_zoom_at_point(image_origin_x, image_origin_y,
				    zoom,
				    image_width, image_height,
				    screen_width, screen_height,
				    image_frame,
				    image);
   
  SDL_UnlockTexture(texture);  
  SDL_RenderCopy(renderer,texture,NULL,NULL);
  SDL_RenderPresent(renderer);

  off_screen_frame =
    (unsigned int*)malloc(sizeof(unsigned int)*screen_width*screen_height);
  
  while(1) {

    clock_gettime(CLOCK_MONOTONIC, &start);

    while(SDL_PollEvent(&event)) {

      switch(event.type) {

      case SDL_QUIT:
	graceful_exit(thread_data,quit_delay);
	goto finish;
	
	break;

      case SDL_WINDOWEVENT:
	if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
	  
	  redraw(renderer, texture,
		 image, pitch,
		 screen_width,screen_height,
		 image_origin_x, image_origin_y,
		 image_width, image_height,
		 zoom);
	  if(off_screen_frame_available) off_screen_frame_available = 0;
	  
	} else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
	  graceful_exit(thread_data,quit_delay);
	  goto finish;
	}
	break;

      case SDL_MOUSEBUTTONDOWN:

	if (event.button.button == SDL_BUTTON_LEFT) {

	  rect_start_x = event.button.x;
	  rect_start_y = event.button.y;
	  
	  mouse_left_down = 1;
	} else if (event.button.button == SDL_BUTTON_MIDDLE) {

	  shift_start_x = event.button.x;
	  shift_start_y = event.button.y;

	  mouse_middle_down = 1;	  
	}
	break;

      case SDL_MOUSEBUTTONUP:

	if (event.button.button == SDL_BUTTON_LEFT) {

	  if(off_screen_frame_available) {
	    if(abs(rect_start_x - rect_mouse_x) > 4 &&
	       abs(rect_start_y - rect_mouse_y) > 4) {

	      pthread_mutex_lock(&lock);
	      currently_active = thread_data->active;
	      pthread_mutex_unlock(&lock);
	      if(currently_active) {
		fprintf(stderr,"Request in Progress, hit q to cancel...\n");
	      } else {
		
		out_image = extract_image(off_screen_frame,
					  screen_width, screen_height,
					  rect_start_x, rect_start_y,
					  rect_mouse_x, rect_mouse_y,
					  zoom,
					  &image_out_sizex, &image_out_sizey);

		thread_data->image = out_image;
		thread_data->width = image_out_sizex;
		thread_data->height = image_out_sizey;
		thread_data->hostname = hostname;
		thread_data->port = port;

		pthread_mutex_lock(&lock);
	        thread_data->active = 1;
		thread_data->kill = 0;
		pthread_mutex_unlock(&lock);

		pthread_create(&thread,NULL,invoke_model_thread,thread_data);
		pthread_detach(thread);
	      }
	    }
	  }
	  
	  mouse_left_down = 0;

	} else if (event.button.button == SDL_BUTTON_MIDDLE) {

	  mouse_middle_down = 0;
	  off_screen_frame_available = 0;

	}
	break;
	
      case SDL_KEYDOWN:
	
	if (event.key.keysym.sym == SDLK_1) {
	  zoom = 1;

	  redraw(renderer, texture,
		 image, pitch,
		 screen_width,screen_height,
		 image_origin_x, image_origin_y,
		 image_width, image_height,
		 zoom);
	  if(off_screen_frame_available) off_screen_frame_available = 0;
	  
	} else if (event.key.keysym.sym == SDLK_2) {
	  zoom = 2;

	  redraw(renderer, texture,
		 image, pitch,
		 screen_width,screen_height,
		 image_origin_x, image_origin_y,
		 image_width, image_height,
		 zoom);
	  if(off_screen_frame_available) off_screen_frame_available = 0;

	} else if (event.key.keysym.sym == SDLK_3) {
	  zoom = 3;

	  redraw(renderer, texture,
		 image, pitch,
		 screen_width,screen_height,
		 image_origin_x, image_origin_y,
		 image_width, image_height,
		 zoom);
	  if(off_screen_frame_available) off_screen_frame_available = 0;
	  
	} else if (event.key.keysym.sym == SDLK_q) {
	  pthread_mutex_lock(&lock);
	  currently_active = thread_data->active;
	  pthread_mutex_unlock(&lock);
	  if (thread_data != 0 && currently_active) {
	    fprintf(stderr,"Request Cancelled!\n");
	    pthread_mutex_lock(&lock);
	    thread_data->kill = 1;
	    pthread_mutex_unlock(&lock);
	  }

	} else if (event.key.keysym.sym == SDLK_ESCAPE) {
	  graceful_exit(thread_data,quit_delay);
	  goto finish;
	}
	
	break;
	
      }
    }
    
    if (mouse_left_down) {
      
      SDL_GetMouseState(&rect_mouse_x,&rect_mouse_y);
      
      if(position_inside(screen_width,
			 screen_height,
			 rect_mouse_x,
			 rect_mouse_y)) {
	
	SDL_LockTexture(texture, NULL, (void**)&image_frame, &pitch);
	
	if (off_screen_frame_available) {
	  
	  memcpy(image_frame,
		 off_screen_frame,
		 sizeof(unsigned int)*screen_width*screen_height);
	  
	} else {
	  
	  
	  memset(image_frame,0,sizeof(int)*screen_width*screen_height);
	  
	  image_to_frame_with_zoom_at_point(image_origin_x, image_origin_y,
					    zoom,
					    image_width, image_height,
					    screen_width, screen_height,
					    image_frame,
					    image);
	  
	  memcpy(off_screen_frame,
		 image_frame,
		 sizeof(unsigned int)*screen_width*screen_height);
	  
	  off_screen_frame_available = 1;
	  
	}
	
	draw_rect(image_frame,
		  rect_start_x,rect_start_y,
		  rect_mouse_x,rect_mouse_y,screen_width, screen_height);
	
	SDL_UnlockTexture(texture);
	SDL_RenderCopy(renderer,texture,NULL,NULL);
	
      }
      
    } else if (mouse_middle_down) {
      
      SDL_GetMouseState(&shift_mouse_x,&shift_mouse_y);
      
      image_origin_x += shift_start_x - shift_mouse_x;
      image_origin_y += shift_start_y - shift_mouse_y;
      
      if(image_origin_x < 0) image_origin_x = 0;
      if(image_origin_y < 0) image_origin_y = 0;
      
      if(image_origin_x > image_width - 1) image_origin_x = image_width - 1;
      if(image_origin_y > image_height - 1) image_origin_y = image_height - 1;
      
      if(off_screen_frame_available) off_screen_frame_available = 0;
      
      SDL_LockTexture(texture, NULL, (void**)&image_frame, &pitch);

      memset(image_frame,0,sizeof(int)*screen_width*screen_height);
      
      image_to_frame_with_zoom_at_point(image_origin_x, image_origin_y,
					zoom,
					image_width, image_height,
					screen_width, screen_height,
					image_frame,
					image);
      
      SDL_UnlockTexture(texture);
      SDL_RenderCopy(renderer,texture,NULL,NULL);
      
    }

    SDL_RenderPresent(renderer);
    
    clock_gettime(CLOCK_MONOTONIC,&end);
    loop_duration =
      (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    loop_duration = labs(loop_duration);
    if (loop_duration < TARGET_LOOP_DURATION_SECONDS * 1e9) {
      loop_delay.tv_nsec =
	(long)(TARGET_LOOP_DURATION_SECONDS * 1e9 - loop_duration);
      nanosleep(&loop_delay, NULL);
    }

  }
 finish :
  if(off_screen_frame) {
    free(off_screen_frame);
  }
  free(image);
  SDL_DestroyWindow(window);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  
  SDL_Quit();
  return(0);
}
  
 
			     
