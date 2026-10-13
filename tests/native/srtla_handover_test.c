/* Host regression test for the actual Android sender (Android/JNI logging stubbed). */
#include <fcntl.h>
#define main srtla_command_main
#include "../../srtla-lib/src/main/cpp/srtla_send_android.c"
#undef main

int srtla_is_java_owned_fd(int fd) { (void)fd; return 1; }
void srtla_on_connection_established(void) {}

static void write_ips(const char *path, int wifi) {
    FILE *file = fopen(path, "w");
    assert(file);
    if (wifi) fputs("10.0.1.1\n", file);
    fputs("10.0.2.1\n", file);
    assert(fclose(file) == 0);
}

static conn_t *connection(const char *ip) {
    struct sockaddr address;
    memset(&address, 0, sizeof(address));
    assert(parse_ip((struct sockaddr_in *)&address, (char *)ip) == 0);
    return conn_find_by_src(&address);
}

static int udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    assert(fd >= 0);
    return fd;
}

static void *publish_mappings(void *unused) {
    (void)unused;
    for (int i = 0; i < 1000; i++) {
        srtla_set_network_socket("10.0.9.1", "192.0.2.9", 1, i);
    }
    return NULL;
}

static int read_stats = 1;
static void *poll_stats(void *unused) {
    (void)unused;
    while (__atomic_load_n(&read_stats, __ATOMIC_ACQUIRE)) {
        srtla_lock_connections();
        assert(srtla_get_connection_count() >= 1);
        srtla_get_total_in_flight_packets();
        srtla_unlock_connections();
    }
    return NULL;
}

int main(void) {
    char path[] = "/tmp/srtla-handover-XXXXXX";
    int config_fd = mkstemp(path);
    assert(config_fd >= 0);
    close(config_fd);
    FD_ZERO(&active_fds);
    max_act_fd = -1;
    int wifi_fd = udp_socket();
    int cellular_fd = udp_socket();
    srtla_set_network_socket("10.0.1.1", "192.0.2.1", 1, wifi_fd);
    srtla_set_network_socket("10.0.2.1", "192.0.2.2", 2, cellular_fd);
    write_ips(path, 1);
    assert(setup_conns(path) == 2);
    assert(open_conns(NULL, NULL) == 2);
    conn_t *wifi = connection("10.0.1.1");
    conn_t *cellular = connection("10.0.2.1");
    assert(wifi && cellular);
    time_t now;
    assert(get_seconds(&now) == 0);
    wifi->last_rcvd = cellular->last_rcvd = now;
    wifi->in_flight_pkts = 100;
    cellular->in_flight_pkts = 7;
    int cell_window = cellular->window;
    pending_reg2_conn = wifi;

    // New Network, same virtual transport IP: replace only its FD and registration.
    int replacement_fd = udp_socket();
    srtla_set_network_socket("10.0.1.1", "192.0.2.3", 1, replacement_fd);
    update_conns(path);
    assert(connection("10.0.1.1") == wifi);
    assert(wifi->fd == -1 && wifi->last_rcvd == 0 && wifi->in_flight_pkts == 0);
    assert(pending_reg2_conn == NULL);
    assert(fcntl(wifi_fd, F_GETFD) == -1);
    assert(open_socket(wifi, 0) == 0 && wifi->fd == replacement_fd);
    assert(strcmp(wifi->real_ip, "192.0.2.3") == 0);
    assert(cellular->fd == cellular_fd && cellular->in_flight_pkts == 7);
    assert(cellular->window == cell_window);

    // A stalled cellular data path must not steal packets from a healthy Wi-Fi link.
    // Only earned ACK (not uncorrelated keepalive) may revive a stalled path.
    struct timespec monotonic_now;
    assert(clock_gettime(CLOCK_MONOTONIC, &monotonic_now) == 0);
    wifi->last_rcvd = cellular->last_rcvd = now;
    wifi->in_flight_pkts = 1;
    cellular->in_flight_pkts = 40;
    wifi->last_delivery_proof = monotonic_now;
    cellular->last_delivery_proof = monotonic_now;
    cellular->last_delivery_proof.tv_sec -= 4;
    assert(conn_stalled(cellular, &monotonic_now));
    assert(!conn_stalled(wifi, &monotonic_now));
    assert(select_conn() == wifi);
    // An earned ACK makes the stalled link eligible again.
    cellular->last_delivery_proof = monotonic_now;
    assert(!conn_stalled(cellular, &monotonic_now));
    // Even if all paths stall, preserve single-link fallback.
    wifi->in_flight_pkts = 40;
    wifi->last_delivery_proof.tv_sec -= 4;
    cellular->last_delivery_proof.tv_sec -= 4;
    assert(select_conn() != NULL);
    wifi->in_flight_pkts = 0;
    cellular->in_flight_pkts = 7;

    // Repeated NAKs from one uplink must reduce its selection score, not drop
    // the entire bond or penalize the healthy peer.
    wifi->last_rcvd = cellular->last_rcvd = now;
    wifi->window = cellular->window = 20000;
    wifi->in_flight_pkts = cellular->in_flight_pkts = 0;
    wifi->last_delivery_proof = cellular->last_delivery_proof = monotonic_now;
    wifi->pkt_idx = 1;
    wifi->pkt_log[0] = 12345;
    register_nak(12345);
    assert(wifi->nak_penalty_percent == 8);
    assert(cellular->nak_penalty_percent == 0);
    assert(select_conn() == cellular);
    // A healthy link remains usable even when the other is penalized.
    wifi->window = 20000;
    cellular->window = cell_window;
    wifi->in_flight_pkts = 0;
    cellular->in_flight_pkts = 7;

    // A failed leg must not set aggregate reconnecting while cellular is healthy.
    struct sockaddr_in *destination = (struct sockaddr_in *)&srtla_addr;
    destination->sin_family = AF_INET;
    destination->sin_port = htons(5000);
    destination->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wifi->last_rcvd = 1;
    has_connected = 1;
    is_reconnecting = 1;
    connection_housekeeping();
    assert(active_connections == 1 && is_reconnecting == 0);
    assert(select_conn() == cellular);

    // Remove Wi-Fi: preserve cellular's socket, window and connection object.
    write_ips(path, 0);
    update_conns(path);
    assert(connection("10.0.1.1") == NULL);
    assert(connection("10.0.2.1") == cellular && cellular->fd == cellular_fd);
    assert(fcntl(cellular_fd, F_GETFD) >= 0);
    assert(select_conn() == cellular);
    assert(fcntl(replacement_fd, F_GETFD) == -1);
    schedule_update_conns(0);
    assert(__atomic_exchange_n(&do_update_conns, 0, __ATOMIC_ACQ_REL) == 1);
    schedule_update_conns(0);
    assert(__atomic_exchange_n(&do_update_conns, 0, __ATOMIC_ACQ_REL) == 1);

    // Concurrent JNI publication yields complete snapshots to the sender thread.
    srtla_set_network_socket("10.0.9.1", "192.0.2.9", 1, 0);
    pthread_t publisher;
    assert(pthread_create(&publisher, NULL, publish_mappings, NULL) == 0);
    for (int i = 0; i < 1000; i++) {
        virtual_conn_t snapshot;
        assert(copy_virtual_connection("10.0.9.1", &snapshot));
        assert(strcmp(snapshot.real_ip, "192.0.2.9") == 0);
        assert(snapshot.network_type == 1 && snapshot.socket_fd >= 0);
    }
    assert(pthread_join(publisher, NULL) == 0);
    // The stats panel can poll while a network is repeatedly added and removed.
    pthread_t stats_reader;
    assert(pthread_create(&stats_reader, NULL, poll_stats, NULL) == 0);
    for (int i = 0; i < 100; i++) {
        int fd = udp_socket();
        srtla_set_network_socket("10.0.1.1", "192.0.2.4", 1, fd);
        write_ips(path, 1);
        update_conns(path);
        assert(open_socket(connection("10.0.1.1"), 0) == 0);
        write_ips(path, 0);
        update_conns(path);
    }
    __atomic_store_n(&read_stats, 0, __ATOMIC_RELEASE);
    assert(pthread_join(stats_reader, NULL) == 0);
    close(cellular_fd);
    free(cellular);
    conns = NULL;
    while (virtual_connections) {
        virtual_conn_t *next = virtual_connections->next;
        free(virtual_connections);
        virtual_connections = next;
    }
    unlink(path);
    puts("PASS: socket replacement, surviving link, aggregate status, notifications, concurrent snapshots and telemetry");
    return 0;
}
