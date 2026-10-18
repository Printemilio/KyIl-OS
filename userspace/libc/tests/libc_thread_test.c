/* POSIX threads on native Vex threads and futexes. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include "check.h"

#define THREADS 16
#define ROUNDS 20000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long counter;
static __thread int tls_id;

static void *count_up(void *arg)
{
	tls_id = (int)(long)arg;
	for (int i = 0; i < ROUNDS; i++) {
		pthread_mutex_lock(&lock);
		counter++;
		pthread_mutex_unlock(&lock);
	}
	errno = 1000 + tls_id;
	sched_yield();
	return (void *)(long)(tls_id == (int)(long)arg && errno == 1000 + tls_id);
}

static pthread_mutex_t cond_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int turn, exchanges;

static void *ping_pong(void *arg)
{
	int me = (int)(long)arg;
	for (int i = 0; i < 2000; i++) {
		pthread_mutex_lock(&cond_lock);
		while (turn != me) pthread_cond_wait(&cond, &cond_lock);
		turn = !me;
		exchanges++;
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&cond_lock);
	}
	return NULL;
}

static pthread_barrier_t barrier;
static int serial_count;

static void *barrier_worker(void *arg)
{
	(void)arg;
	for (int i = 0; i < 50; i++)
		if (pthread_barrier_wait(&barrier) == PTHREAD_BARRIER_SERIAL_THREAD)
			__atomic_add_fetch(&serial_count, 1, __ATOMIC_RELAXED);
	return NULL;
}

static volatile int detached_done;

static void *detached(void *arg)
{
	(void)arg;
	__atomic_store_n(&detached_done, 1, __ATOMIC_RELEASE);
	return NULL;
}

static long long ns_since(const struct timespec *a)
{
	struct timespec b;
	clock_gettime(CLOCK_MONOTONIC, &b);
	return (b.tv_sec - a->tv_sec) * 1000000000ll + (b.tv_nsec - a->tv_nsec);
}

int main(void)
{
	pthread_t t[THREADS];
	void *result;

	/* Contended mutex across all CPUs; per-thread TLS and errno. */
	for (long i = 0; i < THREADS; i++) CHECK(pthread_create(&t[i], NULL, count_up, (void *)i) == 0);
	int ok = 1;
	for (int i = 0; i < THREADS; i++) {
		CHECK(pthread_join(t[i], &result) == 0);
		ok &= result == (void *)1;
	}
	CHECK(ok);
	CHECK_EQ_INT(counter, (long)THREADS * ROUNDS);

	/* Condition variables. */
	pthread_t a, b;
	CHECK(pthread_create(&a, NULL, ping_pong, (void *)0) == 0);
	CHECK(pthread_create(&b, NULL, ping_pong, (void *)1) == 0);
	CHECK(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
	CHECK_EQ_INT(exchanges, 4000);

	/* A timed wait times out, close to its deadline. */
	struct timespec start, deadline;
	clock_gettime(CLOCK_MONOTONIC, &start);
	pthread_condattr_t ca;
	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_t timed;
	pthread_cond_init(&timed, &ca);
	deadline = start;
	deadline.tv_nsec += 50 * 1000 * 1000;
	if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
	pthread_mutex_lock(&cond_lock);
	CHECK_EQ_INT(pthread_cond_timedwait(&timed, &cond_lock, &deadline), ETIMEDOUT);
	pthread_mutex_unlock(&cond_lock);
	long long waited = ns_since(&start);
	CHECK(waited >= 50000000ll);
	CHECK(waited < 500000000ll);
	fprintf(stderr, "  timedwait 50 ms took %lld us\n", waited / 1000);

	/* Barriers. */
	CHECK(pthread_barrier_init(&barrier, NULL, 4) == 0);
	for (int i = 0; i < 4; i++) CHECK(pthread_create(&t[i], NULL, barrier_worker, NULL) == 0);
	for (int i = 0; i < 4; i++) CHECK(pthread_join(t[i], NULL) == 0);
	CHECK_EQ_INT(serial_count, 50);

	/* Semaphores and a trylock that must fail. */
	sem_t sem;
	CHECK(sem_init(&sem, 0, 0) == 0);
	CHECK(sem_trywait(&sem) < 0 && errno == EAGAIN);
	CHECK(sem_post(&sem) == 0 && sem_wait(&sem) == 0);
	pthread_mutex_lock(&lock);
	CHECK_EQ_INT(pthread_mutex_trylock(&lock), EBUSY);
	pthread_mutex_unlock(&lock);

	/* Detached threads clean up by themselves; many short threads. */
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t d;
	CHECK(pthread_create(&d, &attr, detached, NULL) == 0);
	for (int i = 0; i < 200 && !__atomic_load_n(&detached_done, __ATOMIC_ACQUIRE); i++) usleep(1000);
	CHECK(detached_done);
	for (int round = 0; round < 64; round++) {
		CHECK(pthread_create(&a, NULL, detached, NULL) == 0);
		CHECK(pthread_join(a, NULL) == 0);
	}

	/* Large stacks. */
	pthread_attr_t big;
	pthread_attr_init(&big);
	CHECK(pthread_attr_setstacksize(&big, 8u << 20) == 0);
	CHECK(pthread_create(&a, &big, detached, NULL) == 0 && pthread_join(a, NULL) == 0);

	return check_report("libc-thread");
}
