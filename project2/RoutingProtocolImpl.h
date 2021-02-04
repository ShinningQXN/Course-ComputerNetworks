#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include "Node.h"
#include <map>
using std::map;

class RoutingProtocolImpl : public RoutingProtocol {
  public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from
    // a neighbor router.

 private:
    /*** since 'sys' is already declared in the base class,
      why repeat it here... ***/
    //Node *sys; // To store Node object; used to access GSR9999 interfaces

    struct packet_t {
        enum { PACKET_HEADER_SIZE = 8 };

        ePacketType type : 8;
        int reserved : 8;
        int size : 16;
        int src_id : 16;
        int dst_id : 16;
        union {
            // routing protocal update packet data
            struct { unsigned short node_id, cost; } DV_data[0];
            struct {
                unsigned int sequence_number;
                struct { unsigned short neighbor_id, cost; } LS_data[0];
            };
            unsigned int u32;
        };
    };

    // The auxilary data passed through to the alarm handler
    enum alarm_type {
        ALARM_PING,
        ALARM_ROUTING,
        ALARM_LINK
    };

    /* Define a type for router id represented in NETWORK order.
      Note that the internal storage of router IDs are all in network order,
      which faciliate the management of router information.*/
    typedef unsigned short  id_netorder_t;

    unsigned short num_ports;       // store number of ports
    id_netorder_t router_id;        // store router id in NETWORK order
    eProtocolType protocol_type;    // store the protocal_type: P_DV or P_LS

    // Data structure to maintain the port status
    struct port_status_t {
        enum { TIMEOUT = 15 };

        unsigned int cost;  // delay in ms
        int timeout;        // timeout/TTL/age/..., count downto 0 from 15
        id_netorder_t id;

        port_status_t(): cost(INFINITY_COST) {}
        // operator for finding a port using a router id
        bool operator==(id_netorder_t id_to_find) const {
            return id == id_to_find;
        }
    };
    vector<port_status_t> port_status;

    // commonly used forwarding table among DV and LS
    map <id_netorder_t, unsigned short> forwarding_table;

    /// Distance Vector Impl ///
    bool b_send_incremental_DV;
    struct DV_entry_t {
        enum { DV_TIMEOUT = 45 };

        //id_netorder_t dst;
        //id_netorder_t next_hop;
        unsigned int cost;
        int timeout;
        bool changed;

        DV_entry_t(): cost(INFINITY_COST), /*timeout(DV_TIMEOUT),*/ changed(false) {}
    };
    map <id_netorder_t, DV_entry_t> DV_table;

    /** Functions **/
    void send_DV_update();
    void send_DV_update_changed_only(int n_changed);

    /// Link State Impl ///
    struct LS_entry_t {
        enum { LS_TIMEOUT = 45 };

        // edges represented as <id, cost, port> tuple
        /*struct neighbor_t {
            unsigned int cost;
            unsigned int port;
        };
        //map<id_netorder_t, pair<unsigned,unsigned> > neighbors;
        map<id_netorder_t, neighbor_t> neighbors;*/
        map<id_netorder_t, unsigned int> neighbors;
        int timeout;
        unsigned int sequence_number;   // current sequence number
        /* The below two are used in function SSSP to store intermediate
          information about the shortest paths. */
        unsigned int cost;
        //id_netorder_t parent;
        unsigned int port;  // save 'port' number instead of 'parent' id, make life easier

        LS_entry_t(): timeout(LS_TIMEOUT), sequence_number(0), cost(INFINITY_COST) {}
    };
    /* We are using a 'std::priority_queue' in the Dijkstra's algorithm,
      thus need a custom comparator for queue items. */
    typedef map <id_netorder_t, LS_entry_t>::iterator   LS_iterator;
    typedef pair<unsigned int, LS_iterator>     LS_item_t;
    struct LS_comparator {
        bool operator()(const LS_item_t &a, const LS_item_t &b) {
            return a.first > b.first;
        }
    };
    map<id_netorder_t, LS_entry_t> LS_nodes;
    // operator< used by priority_queue
    // single source shortest path, e.g. Dijkstra's Algorithm
    void LS_SSSP();
    void send_LS_update();
};

#endif

