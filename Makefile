all: zxfy

zxfy: zxfy.c
	$(CC) -O3 zxfy.c `libpng-config --cflags` `libpng-config --L_opts` `libpng-config --libs` `sdl2-config --cflags` `sdl2-config --libs` -lm -o zxfy -Wall -W

clean:
	rm -f zxfy
