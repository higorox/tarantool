#include "scramble.h"
#include "random.h"
#include "third_party/sha1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main()
{
	random_init();
	int salt[SCRAMBLE_SIZE/sizeof(int)];
	for (int i = 0; i < sizeof(salt)/sizeof(int); i++)
		salt[i] = rand();

	char *password = "lechododilikraskaloh";
	unsigned char hash2[SCRAMBLE_SIZE];

	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, (unsigned char *) password, strlen(password));
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	char scramble[SCRAMBLE_SIZE];

	scramble_prepare(scramble, salt, password, strlen(password));

	printf("%d\n", scramble_check(scramble, salt, hash2));

	password = "wrongpass";
	scramble_prepare(scramble, salt, password, strlen(password));

	printf("%d\n", scramble_check(scramble, salt, hash2) != 0);

	scramble_prepare(scramble, salt, password, 0);

	printf("%d\n", scramble_check(scramble, salt, hash2) != 0);

	return 0;
}
