#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define PROGRAMMING_PERIOD 0.002
#define TUTORING_PERIOD 0.2

int help, chairs, empty_chairs;
int* visit_counts, last_visit_time, have_chair;
pthread_mutex_t chair_lock, visit_lock;
sem_t* students_need_help, students_waiting_tutors, tutors_waiting_students, student_with_tutor;

unsigned int
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec;
}

static void*
student(void *id) {
  int student_id = (int) id;

  int h = help;
  int period;
  while (h != 0) {
    // programming phase of student
    period = (rand() % (PROGRAMMING_PERIOD * 1000 + 1)) * 1000;
    sleep(period);

    // visit csmc for help
    pthread_mutex_lock(&visit_lock);
    visit_counts[student_id]++;
    last_visit_time[student_id] = now();
    pthread_mutex_unlock(&visit_lock);

    // search for an empty chair
    pthread_mutex_lock(&chair_lock);
    if (empty_chairs == 0) {
      have_chair[student_id] = 0;
      pthread_mutex_unlock(&chair_lock);
      continue;
    }
    empty_chairs--;
    have_chair[student_id] = 1;
    pthread_mutex_unlock(&chair_lock);

    // found an empty chair
    // notify coordinator using semaphore
    sem_post(&students_need_help);
    // wait for tutor
    pthread_mutex_lock(&chair_lock);
    empty_chairs++;
    pthread_mutex_unlock(&chair_lock);

    // sleep for tutoring phase
    sem_wait(&student_with_tutor[student_id]);
    h--;
  }

  return NULL;
}

static void*
coordinator(void *args) {
  pthread_mutex_lock(&chair_lock);
  empty_chairs = chairs;
  pthread_mutex_unlock(&chair_lock);

  while (1) {
    // wait for student with semaphore
    sem_wait(&students_need_help);
    // wait for idle tutor 
    sem_wait(&tutors_waiting_students);
    // notify the tutor
    sem_post(&students_waiting_tutors);
  }
  return NULL;
}

static void*
tutor(void *id) {
  int tutor_id = (int) id;
  int period;

  while (1) {
    // let coordinator know tutor is free
    sem_post(&tutors_waiting_students);

    // wait for coordinator
    sem_wait(&students_waiting_tutors);

    // find student with highest priority needing help

    // help the student and release him
    period = (rand() % (TUTORING_PERIOD * 1000 + 1)) * 1000;
    sleep(period);
    sem_post(&student_with_tutor[student_id]);
  }
  return NULL;
}


int 
main(int argc, char* argv[]) {
  if (argc != 4) {
    printf("Need 4 arguments");
    return -1;
  }

  int i;

  int students = atoi(argv[0]);
  int tutors = atoi(argv[1]);
  int chairs = atoi(argv[2]);
  int help = atoi(argv[3]);
  
  assert(pthread_mutex_init(&chair_lock, NULL) == 0);
  assert(pthread_mutex_init(&visit_lock, NULL) == 0);

  sem_init(&students_need_help, 0);
  sem_init(&students_waiting_tutors, 0);
  sem_init(&tutors_waiting_students, 0);
  sem_init(&student_with_tutor, 0);

  visit_counts = malloc(sizeof(int) * students);
  last_visit_time = malloc(sizeof(int) * students);
  have_chair = malloc(sizeof(int) * students);
  student_with_tutor = malloc(sizeof(sem_t) * students);
  pthread_t *student_threads = malloc(sizeof(pthread_t) * students);
  for(i = 0; i < students; i++) {
    assert(pthread_create(&student_threads[i], NULL, student, (void *) i) == 0);
    sem_init(&student_with_tutor[i], 0);
  }

  pthread_t *tutor_threads = malloc(sizeof(pthread_t) * tutors);
  for(i = 0; i < tutors; i++) {
    assert(pthread_create(&tutor_threads[i], NULL, tutor, (void *) i) == 0);
  }

  pthread_t coordinator_thread;

  assert(pthread_create(&coordinator_thread, NULL, coordinator, NULL) == 0);

  return 0;

}
