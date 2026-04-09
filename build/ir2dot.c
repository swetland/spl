// Copyright 2026, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <string.h>
#include <ctype.h>

char *trim(char *s);
char *after(char *s, char *mark);
char *next(char **s);

void bb_start(char *bb, char *tag, char *plist) {
	char *arg;
	while ((arg = next(&plist))) {
		printf("\"%s\" -> \"%s\";\n", arg, bb);
	}
	printf("\"%s\" [ label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n", bb);
	printf("<TR><TD BGCOLOR=\"gray\" ALIGN=\"left\">%s: %s</TD></TR>\n", bb, tag);

}
void bb_inst(char *bb, char *ins) {
	if (ins[0] == '.') return;
	printf("<TR><TD ALIGN=\"left\">%s</TD></TR>\n", ins);
}
void bb_end(char *bb) {
	printf("</TABLE>>; ];\n");
}

int main(int argc, char** argv) {
	char buf[256];
	char *bb = NULL;

	printf("digraph g {\n");
	printf("node [ shape=plain; ];\n");
	for (;;) {
		if (fgets(buf, sizeof(buf), stdin) == NULL) break;
		char *line = trim(buf);
		char *colon = strchr(line, ':');
		if (colon) {
			*colon++ = 0;
			if (bb) bb_end(bb);
			bb = strdup(line);
			char *tag = after(colon, "//");
			char *plist = after(tag, "<<");
			bb_start(trim(bb), trim(tag), trim(plist));
		} else {
			if (bb) bb_inst(bb, trim(line));
		}
	}
	if (bb) bb_end(bb);
	printf("}\n");
	return 0;
}

// remove leading/trailing whitespace
char *trim(char *s) {
	while (isspace(*s)) s++;
	int len = strlen(s);
	while ((len > 0) && isspace(s[len-1])) len--;
	if (len) s[len] = 0;
	return s;
}

// truncate string at mark and return what's after
char *after(char *s, char *mark) {
	if ((s = strstr(s, mark))) {
		*s = 0;
		return s + strlen(mark);
	} else {
		return "";
	}
}

// destructively return the next non-empty
// comma-separated argument or NULL if none
char *next(char **s) {
	char *arg = *s;
	char *comma = strchr(arg, ',');
	if (comma) {
		*comma++ = 0;
		*s = comma;
	} else {
		*s = "";
	}
	arg = trim(arg);
	return (arg[0] == 0) ? NULL : arg;
}

