main: tree.o mpeg.o
	g++ tree.o mpeg.o -std=c++11 -O3 -o mpeg -Lfreeglut/lib/x64 -lfreeglut -lopengl32
x86: tree.o mpeg.o
	g++ tree.o mpeg.o -std=c++11 -O3 -o mpeg -Lfreeglut/lib -lfreeglut -lopengl32
debug: tree.o
	g++ tree.o mpeg.cpp -std=c++11 -g -o mpeg_de
tree.o:
	g++ tree.cpp -c -o tree.o -std=c++11
mpeg.o:
	g++ mpeg.cpp -c -o mpeg.o -Ifreeglut/include -std=c++11
clean:
	rm ./mpeg ./tree.o ./mpeg_de ./mpeg.o
