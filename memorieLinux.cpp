#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <ctime>
#include <sys/wait.h>

#define SHM_NAME "/my_shared_mem_cpp"
#define SEM_NAME "/my_semaphore_cpp"

int main() {
    int shm_fd;
    int *counter;

    // 1) Creăm / deschidem segmentul de memorie partajată
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    // Alocăm memorie pentru un int
    ftruncate(shm_fd, sizeof(int));

    // În mapăm în memoria procesului
    counter = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (counter == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // Inițializăm contorul
    *counter = 0;

    // 2) Creăm semaforul
    sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    // RNG diferit pentru procese
    srand(time(NULL) ^ getpid());

    // 3) Creăm procesul copil
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    // Atât părintele cât și copilul execută acest while
    while (true) {
        sem_wait(sem);  // intrăm în secțiunea critică

        if (*counter >= 1000) {
            sem_post(sem);
            break;
        }

        int coin = rand() % 2;  // 0 sau 1

        if (coin == 1) {
            (*counter)++;
            std::cout << "PID " << getpid() << " wrote: " << *counter << std::endl;
        }

        sem_post(sem);  // ieșim din secțiunea critică

        usleep(1000);
    }

    // Doar părintele curăță resursele
    if (pid > 0) {
        wait(NULL);  // așteptăm copilul

        std::cout << "Final counter = " << *counter << std::endl;

        sem_close(sem);
        sem_unlink(SEM_NAME);

        munmap(counter, sizeof(int));
        shm_unlink(SHM_NAME);
    }

    return 0;
}
