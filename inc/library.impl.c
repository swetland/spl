
void f$_hexout_(int x) {
	printf("D %08x\n", x);
}

int main(int argc, char** argv) {
	int x = f$start();
	printf("X %08x\n", x);
	return 0;
}

