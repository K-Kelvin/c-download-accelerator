.PHONY: all
all: http_downloader

http_downloader: http_downloader.o
	gcc -o $@ $^ -lpthread -lssl -lcrypto

%.o: %.c
	gcc -c $< -o $@

# "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/topnav-sport2_r1_c1.gif"
# "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/Uga-VII.jpg"
# "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/story_hairydawg_UgaVII.jpg"

.PHONY: run1
run1:
	./http_downloader -u "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/topnav-sport2_r1_c1.gif" -o topnav-sport2_r1_c1.gif -n 5

.PHONY: run2
run2:
	./http_downloader -u "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/Uga-VII.jpg" -o Uga-VII.jpg -n 5

.PHONY: run3
run3:
	./http_downloader -u "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/story_hairydawg_UgaVII.jpg" -o story_hairydawg_UgaVII.jpg -n 5

.PHONY: test
test:
	time ./http_downloader -u "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/Uga-VII.jpg" -o Uga-VII.jpg -n 5

.PHONY: clean
clean:
	rm -f *.o

.PHONY: clean_all
clean_all:
	rm -f *.o http_downloader part_* *.gif *.jpg *.png && clear