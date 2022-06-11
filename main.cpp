#include <cstdio>
#include <cstdlib>
#include <mpi.h>
#include <ctime>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <csignal>
#include <algorithm>

#include <vector>
#include <array>
#include <queue>

#include "main.hpp"
#include "watek_komunikacyjny.hpp"

// Ilosc zasobow
constexpr int HOTEL_COUNT = 1;
constexpr int GUIDE_COUNT = 1;

constexpr int HOTEL_OFFSET = 0;
constexpr int GUIDE_OFFSET = HOTEL_COUNT;

constexpr int SLOTS_PER_HOTEL = 1;

// Procentowa ilość danych typów procesów
constexpr int CLEANER_PROC = 20;
constexpr int RED_PROC     = 40;
constexpr int BLUE_PROC    = 40;

constexpr int CLEAN_THRESHOLD = 30;

// liczba pól w Packet_t
constexpr int FIELDNO = 4;

bool isFinished = false;

procType process_state;

// debugging 
int currentlyIn   = -9999;
int currentGuide  = -9999;

// zmienne okreslajace proces
int               size, rank, len;
unsigned          timestamp = 0;
MPI_Datatype      MPI_PAKIET_T;

// vector z ostatnimi otrzymanymi timestampami poszczególnych procesów 
std::vector<unsigned> timestamps;
pthread_mutex_t   timestampsMutex = PTHREAD_MUTEX_INITIALIZER;

// tablica kolejek do poszczególnych zasobów
typedef std::vector<Entry> queue_t;
std::array<queue_t, HOTEL_COUNT + GUIDE_COUNT> queues;

// tablica zliczająca ile razy ktoś skorzystał z zasobu
std::array<unsigned, HOTEL_COUNT + GUIDE_COUNT> queueCount;

pthread_mutex_t   queueMutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    queueCond   = PTHREAD_COND_INITIALIZER;

// wątek komunikacyjny
pthread_t         commThread;

// zmienna zliczajaca ilosc odebranych ACK
unsigned          acks = 0;
pthread_mutex_t   acksMutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t    acksCond    = PTHREAD_COND_INITIALIZER;

// ----------- komunikacja pomiędzy wątkami -------------------------
// funkcja do sortowania timestampów
bool sortByTimestamp(Entry& a, Entry& b) {
    if (a.timestamp == b.timestamp) {
        return a.process_index < b.process_index;
    } else {
        return a.timestamp < b.timestamp;
    }
}

// aktualizuje timestampy kolejki po uzyskaniu ACK
void updateTimestamps(unsigned ts, int process_index) {
    pthread_mutex_lock(&timestampsMutex);
    timestamps[process_index] = ts;
    timestamp = std::max(timestamp, ts) + 1;
    pthread_mutex_unlock(&timestampsMutex);
} 

unsigned incrAcks() {
    pthread_mutex_lock(&acksMutex);
    unsigned ret = ++acks;
    debug("Łącznie ACKS: %d", acks);
    pthread_cond_signal(&acksCond);
    pthread_mutex_unlock(&acksMutex);
    return ret;
}

void addEntry(Entry& entry, int resId) {
    pthread_mutex_lock(&queueMutex);
    //queue_t& queue = queues[resId];
    queues[resId].push_back(entry);

    // TODO: Zamiast sortować wstawić za ostatnim timestampem tak jak było poprzednio
    std::sort(queues[resId].begin(), queues[resId].end(), sortByTimestamp);
    pthread_mutex_unlock(&queueMutex);
}

void rmEntry(int resId, int procIndex) {
    pthread_mutex_lock(&queueMutex);
    auto &queue = queues[resId];

    queue_t::iterator i;
    unsigned index;
    for (i = queue.begin(), index = 0; i < queue.end(); index++, i++) {
        if (i->process_index == procIndex) {
            queue.erase(i);
            break;
        } else if (index == (queue.size() - 1)) {
            debug("Brak elementu do RELEASE!, zasób %d dla [%d]", resId, procIndex);
            pthread_mutex_unlock(&queueMutex);
            return;
        }
    }
    if (resId >= GUIDE_OFFSET) {
        queueCount[resId]++;
        pthread_cond_signal(&queueCond);
    } else if (index < SLOTS_PER_HOTEL) {
        queueCount[resId]++;
        pthread_cond_signal(&queueCond);
    }
    pthread_mutex_unlock(&queueMutex);
}

unsigned getTimestamp(bool change) {
    pthread_mutex_lock(&timestampsMutex);
    if (change) ++timestamp;
    unsigned ts = timestamp;
    pthread_mutex_unlock(&timestampsMutex);
    return ts;
}
// -----------------------------------------------------------------

// wersja tymczasowa TODO: ogarnac
int chooseResource(unsigned offset) {
    pthread_mutex_lock(&queueMutex);
    // indeksy posortowane rozmiarami kolejki 
    auto cmp = [](int left, int right) {
        return queues[left].size() >= queues[right].size();
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> order(cmp);

    /* typ zasobu - hotel lub przewodnik */
    // HOTEL
    if (offset == HOTEL_OFFSET) {
        for (unsigned id = 0; id < HOTEL_COUNT; id++) {
            auto& queue = queues[id];
            size_t length = queue.size();
            if (length == 0) {
                pthread_mutex_unlock(&queueMutex);
                return id;
            } else {
                order.push(id);
            }
        }
        /* sprawdzenie koloru - wybieramy kolejke ktora ma najmniejsza liczbe elementow
         *  o kolorach obecnego procesu, jeżeli takiej kolejki nie ma - 
         *  wybieramy kolejkę o najmniejszej liczbie wpisów */
        int front = order.top();
        for (; !order.empty(); order.pop()) {
            auto& it = order.top();
            for (auto& entry: queues[it]) {
                if (entry.type != process_state) {
                    break;
                }
                if (&entry == &queues[it].back()) {
                    pthread_mutex_unlock(&queueMutex);
                    return it;
                }
            }
        }
        pthread_mutex_unlock(&queueMutex);
        return front;
    }
    // PRZEWODNICY
    else {
        unsigned bestId = GUIDE_OFFSET;

        for (unsigned id = GUIDE_OFFSET; id < GUIDE_COUNT + GUIDE_OFFSET; id++) {
            auto& queue = queues[id];
            size_t length = queue.size();
            if (length == 0) {
                pthread_mutex_unlock(&queueMutex);
                return id;
            } else if (length < queues[bestId].size()) {
                bestId = id;
            }
        }
        pthread_mutex_unlock(&queueMutex);
        return bestId;
    }
}

// ta funkcja nie odblokowuje mutexa!
int chooseHotelToClean() {
    std::vector<unsigned> availIDs;
    for (unsigned i = 0; i < HOTEL_COUNT; i++) {
        for (auto& entry: queues[i]) {
            if (entry.type == CLEANER) {
                break;
            } else if (&entry == &queues[i].back() && queueCount[i] > CLEAN_THRESHOLD) {
                availIDs.push_back(i);
            }
        }
    }

    if (availIDs.size() != 0) {
        unsigned val = rand() % availIDs.size();
        queueCount[val] = 0;
        return availIDs[val]; 
    } else  return -1;
}

// Wrapper na wysyłanie pakietów ------------------------------------------
void sendPacket(Packet_t& pkt, int destination, int tag) {
    MPI_Send(&pkt, 1, MPI_PAKIET_T, destination, tag, MPI_COMM_WORLD);
}

void broadcastPacket(Packet_t& pkt, int tag, bool nowait = false) {
    for (unsigned i = 0; i < size; i++)    sendPacket(pkt, i, tag);
}

// wysyla do wszystkich i czeka na odbior ACK
void broadcastAndAcks(Packet_t& pkt, int tag) { 
    pthread_mutex_lock(&acksMutex);
    acks = 0;
    broadcastPacket(pkt, tag);
    /* Odbiór ACK i sortowanie kolejki w wątku komunikacyjnym
     *  kontynuacja kiedy otrzymamy ACK od kazdego procesu jak odpowiedzą */
    while (acks < size) {
        pthread_cond_wait(&acksCond, &acksMutex);
    }
    pthread_mutex_unlock(&acksMutex);
}

Packet_t prepareACK(int index) {
    return Packet_t {
        .timestamp  = getTimestamp(), 
        .type       = process_state,
        .index      = index,
        .src        = rank
    };
}

Packet_t prepareRequest(int index) {
    return Packet_t {
        .timestamp  = timestamp, 
        .type       = process_state,
        .index      = index,
        .src        = rank
    };
}

void broadcastRelease(int resourceID) {
    Packet_t rel_packet = Packet_t {
        .timestamp  = timestamp,
        .type       = process_state,
        .index      = resourceID,
        .src        = rank
    };

    getTimestamp();

    // bez ack?
    broadcastPacket(rel_packet, RELEASE);
}
// ------------------------------------------------------------------------

// Funkcja wykonywania po uzyskaniu SIGINT
void funcINT() {
    debug("------------------ Zamykanie programu... ------------------");
    isFinished = true;
    Packet_t pkt;
    sendPacket(pkt, rank, FINISH);
}
// debug print kolejki
void debugPrintQueue(unsigned hotelID) {
    auto& queue = queues[hotelID];
    debug("  Kolejka dostepu do hotelu %d:", hotelID);
    for (unsigned i = 0; i < queue.size(); i++) {
        debug("     [%d], idx: %d, kolor: %d, timestamp: %d", 
                queue[i].process_index, i, 
                (int)queue[i].type, queue[i].timestamp
             );
    }
}

// sprawdzenie czy wszystkie procesy maja timestamp wiekszy niz requesta
bool checkAcks(unsigned req_timestamp) {
    pthread_mutex_lock(&timestampsMutex);
    for (unsigned i = 0; i < size; i++) {
        if (req_timestamp >= timestamps[i]) {
            pthread_mutex_unlock(&timestampsMutex);
            debug("Anulowanie rezerwacji, bo [%d] stampy: %d >= %d", 
                    i, req_timestamp, timestamps[i]);
            return false;
        }
    }
    pthread_mutex_unlock(&timestampsMutex);
    return true;
}

// zwraca true jeżeli obecny proces jest na szczycie kolejki żądań
inline bool procAtQueueTop(queue_t& queue) {
    return queue.front().process_index == rank;
}

// sprawdza czy w kolejce przed procesem nie znajduje się sprzątacz
bool checkCleaners(queue_t& queue) {
    for (auto& entry: queue) {
        if (entry.process_index == rank) return true;
        else if (entry.type == CLEANER) return false;
    }
    return true;
}

// procedura wejścia do sekcji krytycznej dla zasobu - Przewodnika
void getGuide() {
    int guideID = chooseResource(GUIDE_OFFSET);
    debug("Proces o kolorze: %d wybrał przewodnika %d", (int)process_state, guideID);

    getTimestamp();

    Packet_t req_packet = prepareRequest(guideID);
    broadcastAndAcks(req_packet, REQUEST_G);

    if (!checkAcks(req_packet.timestamp)) {
        broadcastRelease(guideID);
        return;
    }
    pthread_mutex_lock(&queueMutex);
    /* jezeli proces nie jest na szczycie kolejki zadan, to czekamy na otrzymanie 
     * jakiegos release po czym sprawdzamy czy juz znajduje sie na szczycie */
    while(!procAtQueueTop(queues[guideID])) {
        pthread_cond_wait(&queueCond, &queueMutex);
    }
    pthread_mutex_unlock(&queueMutex);

    currentGuide = guideID;
    debug("Proces o kolorze: %d zabrał przewodnika %d", (int)process_state, guideID);
    usleep(rand() % 10'000 + 2'000);

    currentGuide = -99999;
    broadcastRelease(guideID);
}
/* zwraca true jeżeli jest wolne miejsce w hotelu, oraz w kolejce przed procesem 
 * nie ma innych typów procesów */
bool checkSlotColours(queue_t& queue) {
    for (unsigned i = 0; i < queue.size() && i < SLOTS_PER_HOTEL; i++) {
        if (queue[i].process_index == rank) {
            return true;
        } else if (queue[i].type != process_state) {
            debug("Wykryto kosmitę o innym kolorze!");
            return false;
        }
    }
    return false;
}

// --------------------------------- główna pętla dla kosmitow -----------------------------
void alienProcedure() {
    while (!isFinished) {
        // spimy randomowa dlugosc 
        usleep(rand() % 2'000);

        int hotelID = chooseResource(HOTEL_OFFSET);
        debug("Proces o kolorze: %d wybrał hotel %d", (int)process_state, hotelID);
        // requestujemy miejsce w kolejce do wszystkich procesow
        getTimestamp();

        Packet_t req_packet = prepareRequest(hotelID);
        broadcastAndAcks(req_packet, REQUEST_H);

        // sprawdzenie czy acks maja nowszy timestamp
        if (!checkAcks(req_packet.timestamp)) {
            broadcastRelease(hotelID);
            continue;
        }

        pthread_mutex_lock(&queueMutex);
        auto& queue = queues[hotelID];

        debugPrintQueue(hotelID);
        /* Sprawdzenie czy w kolejce do hotelu nie ma innego koloru przed obecnym procesem
         * jeżeli nie - proces wchodzi do hotelu */
        while (!checkSlotColours(queue)) {
            pthread_cond_wait(&queueCond, &queueMutex);
        }
        currentlyIn = hotelID;
        debug("===== Proces %d wchodzi do hotelu %d o kolorze: %d =====", 
                rank, hotelID, (int)process_state);      

        pthread_mutex_unlock(&queueMutex);
        getGuide();

        currentlyIn = -99999;
        broadcastRelease(hotelID);
    }
}

// ----------------------------------- główna pętla dla sprzątaczy -------------------------
void cleanerProcedure() {
    while (!isFinished) {
        int hotelID;
        // czekamy aż znajdzie się jakiś hotel który przekroczy próg wejść 
        pthread_mutex_lock(&queueMutex);
        while ((hotelID = chooseHotelToClean()) == -1) {
            pthread_cond_wait(&queueCond, &queueMutex);
        }
        pthread_mutex_unlock(&queueMutex);

        Packet_t req_packet = prepareRequest(hotelID);
        broadcastAndAcks(req_packet, REQUEST_H);

        if (!checkAcks(req_packet.timestamp)) {
            broadcastRelease(hotelID);
            continue;
        }

        pthread_mutex_lock(&queueMutex);
        auto& queue = queues[hotelID];
        /* Ostatnie sprawdzenie czy przed sprzątaczem nie znajduje się inny sprzątacz
         * jeżeli tak to wychodzimy awaryjnie */
        if (!checkCleaners(queue)) {
            pthread_mutex_unlock(&queueMutex);
            broadcastRelease(hotelID);
            continue;
        }

        /* jezeli proces nie jest na szczycie kolejki zadan, to czekamy na otrzymanie 
         * jakiegos release po czym sprawdzamy czy juz znajduje sie na szczycie */
        while(!procAtQueueTop(queues[hotelID])) {
            pthread_cond_wait(&queueCond, &queueMutex);
        }

        currentlyIn = hotelID;
        debug("CZYSZCZENIE HOTELU %d", hotelID);
        pthread_mutex_unlock(&queueMutex);
        usleep(rand() % 10'000 + 2'000);
        currentlyIn = -9999;
        broadcastRelease(hotelID);
    }
}

// funkcja przypisująca odpowiedni stan procesowi (na podstawie rank)
void assignState() {
    // SPRZATACZ 
    if (rank < size * CLEANER_PROC / 100) {
        process_state = CLEANER;
    } 
    // FIOLETOWY KOSMITA
    else if (rank < size * (CLEANER_PROC + RED_PROC) / 100) {
        process_state = ALIEN_RED;
    } 
    // BLEKITNY KOSMITA
    else if (rank < size * (CLEANER_PROC + RED_PROC + BLUE_PROC) / 100) {
        process_state = ALIEN_BLUE;
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, (__sighandler_t)&funcINT);
    char processor[100];
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    // tworzenie typu do komunikatu
    const int nitems = FIELDNO;
    int       blocklengths[FIELDNO] = {2, 1, 1, 1};
    MPI_Datatype typy[FIELDNO] = {MPI_UNSIGNED, MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint     offsets[FIELDNO]; 
    offsets[0] = offsetof(Packet_t, timestamp);
    offsets[1] = offsetof(Packet_t, type);
    offsets[2] = offsetof(Packet_t, index);
    offsets[3] = offsetof(Packet_t, src);

    MPI_Type_create_struct(nitems, blocklengths, offsets, typy, &MPI_PAKIET_T);
    MPI_Type_commit(&MPI_PAKIET_T);

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Get_processor_name(processor, &len);

    srand(time(NULL) + rank);
    
    // wypełnianie tablicy zerami
    queueCount.fill(0);
    timestamps = std::vector<unsigned>(size);

    pthread_create(&commThread, NULL, startKomWatek, 0);
    assignState();
    // KOSMITA
    if (process_state != CLEANER) {
        alienProcedure();
    // SPRZĄTACZ
    } else {
        cleanerProcedure();  
    }
    pthread_join(commThread, NULL);

    /* ------ na obecną chwilę to się prawdopodbnie nie wykona ------ */
    pthread_mutex_destroy(&queueMutex);
    pthread_mutex_destroy(&timestampsMutex);
    pthread_mutex_destroy(&acksMutex);

    pthread_cond_destroy(&acksCond);
    pthread_cond_destroy(&queueCond);

    MPI_Type_free(&MPI_PAKIET_T);
    MPI_Finalize();

    return 0;
}
