// RoutingProtocolImpl.cc
//
// by Yuanyi Zhong
// Final editing date: 10/22/2015
//
#include "RoutingProtocolImpl.h"
#include <netinet/in.h>
#include <string.h>

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
  sys = n;
  // add your own code
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
  // add your own code (if needed)
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
    // add your own code
    this->num_ports = num_ports;
    this->router_id = htons(router_id);
    this->protocol_type = protocol_type;

    port_status.resize(num_ports);

    // set and invoke immediately periodic neighborhood dectection
    handle_alarm((void*)ALARM_PING);

    // set routing protocal updates
    //handle_alarm((void*)ALARM_ROUTING);
    /* postpone the first DV "sync" to the time when PONG messages are received.
     in the meantime, send all entries in the DV table in the first 30 seconds.
     see 'simpletest2' for the potential problem if we don't do this */
    sys->set_alarm(this, 30000, (void*)ALARM_ROUTING);
    b_send_incremental_DV = false;
    /*if (protocol_type == P_LS)
        LS_nodes[this->router_id].cost = 0;*/

    // set periodic link status checking
    sys->set_alarm(this, 1000, (void*)ALARM_LINK);
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    // add your own code
    switch (*(int*)&data) {
    case ALARM_PING:
        // reset the alarm
        sys->set_alarm(this, 10000, (void*)ALARM_PING);
        // send PING messages to neighbors
        for (unsigned int port = 0; port < num_ports; ++port) {
            /** SHOULDN'T use 'new'/'delete'!! possible loss of packet,
              during which the simulator calls 'free' rather than 'delete'!!! **/
            packet_t *p = (packet_t*)malloc(packet_t::PACKET_HEADER_SIZE + 4);//new packet_t;
            if (!p) break;///***ERROR
            p->type = PING;
            p->size = htons(packet_t::PACKET_HEADER_SIZE + 4);
            p->src_id = router_id;  // already in network order
            //p->dst_id = ???;  // unused in PING message
            p->u32 = sys->time();
            sys->send(port, p, packet_t::PACKET_HEADER_SIZE + 4);
        }
        break;

    case ALARM_ROUTING:
        sys->set_alarm(this, 30000, (void*)ALARM_ROUTING);
        if (protocol_type == P_DV) {
            b_send_incremental_DV = true;
            send_DV_update();
        }else {
            // if (protocal_type == P_LS)
            send_LS_update();
        }
        break;

    case ALARM_LINK:
        sys->set_alarm(this, 1000, (void*)ALARM_LINK);

        if (protocol_type == P_DV) {

            /* check port status */
            int n_changed = 0;
            // Update DV/LS and forwarding table
            /** for (map <id_netorder_t, DV_entry_t>::iterator it = DV_table.begin();
                 it != DV_table.end(); ++it
                ) it->second.changed = false; **/

            for (unsigned short port = 0; port < num_ports; ++port)
                /* be careful to also check if the link is already dead,
                  avoid repeat the same thing if it is */
                if (port_status[port].timeout <= 0 && port_status[port].cost != INFINITY_COST) {
                    // The link is dead, set the cost to infinity.
                    port_status[port].cost = INFINITY_COST;

                    for (map <id_netorder_t, unsigned short>::iterator it = forwarding_table.begin();
                          it != forwarding_table.end(); ++it) if (it->second == port) {
                        DV_entry_t &entry = DV_table[it->first];
                        entry.cost = INFINITY_COST;
                        //if (entry.changed == false)
                            n_changed++;
                        //entry.changed = true;
                        entry.timeout = DV_entry_t::DV_TIMEOUT;
                        /* check if there's a direct link to node it->first */
                        if (it->first != port_status[port].id) {
                            vector<port_status_t>::iterator it_st =
                                find(port_status.begin(), port_status.end(), it->first);
                            if (it_st != port_status.end()) {
                                entry.cost = it_st->cost;
                                // change the port number in forwarding table
                                it->second = it_st - port_status.begin();
                            }
                        }
                        /** Since second best route is not recorded,
                            impossible to find a better path at this stage.
                        for (unsigned short port_new = 0; port_new < num_ports; ++port_new)
                          if (port_new != port) {
                            unsigned int new_cost = port_status[port_new].cost +
                                ???//DV_table[port_status[port_new].id].cost;
                            if (entry.cost > new_cost) {
                                entry.cost = new_cost;
                                it->second = port_new;
                            }
                        }
                        // No path found
                        if (entry.cost == INFINITY_COST) { } **/
                    }
                }else {
                    port_status[port].timeout--;
                }
            if (n_changed) {
                ///send_DV_update_changed_only(n_changed);
                send_DV_update();
            }

            /* check routing table, remove timeout entries */
            for (map <id_netorder_t, DV_entry_t>::iterator it = DV_table.begin();
                 it != DV_table.end(); ) {
                if (--it->second.timeout < 0) {
                    // out of date, remove routing & forwarding table entry
                    map <id_netorder_t, unsigned short>::iterator it_fw =
                        forwarding_table.find(it->first);
                    if (it_fw != forwarding_table.end())
                        forwarding_table.erase(it_fw);
                    DV_table.erase(it++);
                }else {
                    ++it;
                }
            }
        }else /*if (protocal_type == P_LS)*/ {
            bool b_changed = false;

            /* check port status */
            for (unsigned short port = 0; port < num_ports; ++port)
                if (port_status[port].timeout <= 0 && port_status[port].cost != INFINITY_COST) {
                    // The link is dead, set the cost to infinity.
                    port_status[port].cost = INFINITY_COST;
                    // reflect the change in link states
                    //LS_nodes[port_status[port].id].timeout = 0;
                    LS_nodes[router_id].neighbors.erase(port_status[port].id);
                    LS_entry_t &entry = LS_nodes[port_status[port].id];
                    //LS_nodes[port_status[port].id].neighbors.erase(router_id);
                    entry.neighbors.erase(router_id);
                    /// ***
                    if (entry.neighbors.empty()) {
                        entry.timeout = 0;
                    }
                    b_changed = true;
                }else {
                    port_status[port].timeout--;
                }

            /* check LS table entries */
            for (LS_iterator it = LS_nodes.begin(); it != LS_nodes.end(); )
                /* haven't heard from this node for a long time,
                  assume it has been disconnected from the graph,
                  thus remove all trace about it. */
                if (it->first != router_id && --it->second.timeout < 0) {
                    id_netorder_t id = it->first;
                    // remove from forwarding table
                    map <id_netorder_t, unsigned short>::iterator it_fw =
                        forwarding_table.find(id);
                    if (it_fw != forwarding_table.end())
                        forwarding_table.erase(it_fw);
                    // remove LS table entry
                    LS_nodes.erase(it++);
                    // remove nodes from the graph
                    for (LS_iterator it2 = LS_nodes.begin(); it2 != LS_nodes.end(); ++it2)
                        it2->second.neighbors.erase(id);
                    b_changed = true;
                }else {
                    ++it;
                }

            if (b_changed) {
                LS_SSSP();          // compute new routes
                send_LS_update();   // flood new link state
            }
        }
        break;
    }
}

void RoutingProtocolImpl::recv(unsigned short port_from, void *packet, unsigned short size) {
    // add your own code
    packet_t *p = (packet_t*)packet;
    switch (p->type) {
    case DATA: {
        // current node is the destination
        if (p->dst_id == router_id) {
            //free(p);
            break;//return;
        }
        // originating a packet
        //if (port_from == SPECIAL_PORT) {
        //    sys->send(forwarding_table->dst_id
        //} // receiving a data packet
        //else {
            map <id_netorder_t, unsigned short>::iterator it =
                forwarding_table.find(p->dst_id);
            /* Forward the message according to the table,
              if the corresponding record is found there.*/
            if (it != forwarding_table.end())
                sys->send(it->second, p, size);
            else
                free(p);    // avoid memory leakage
        //}
        return;
    }
    case PING:
        // immediately send the message back
        p->type = PONG;
        p->dst_id = p->src_id;
        p->src_id = router_id;  // 'router_id' is already in network order
        sys->send(port_from, p, size);
        return; //break;

    case PONG: {
        unsigned int cost = sys->time() - p->u32;
        // refresh expiration time
        port_status[port_from].timeout = port_status_t::TIMEOUT;
        unsigned int old_cost = port_status[port_from].cost;
        port_status[port_from].cost = cost;     // cost measured in round-trip delay in ms
        port_status[port_from].id = p->src_id;

        bool b_link_used_in_forwarding = forwarding_table[p->src_id] == port_from;

        if (protocol_type == P_DV) {
            // update DV entry if needed
            DV_entry_t &entry = DV_table[p->src_id];

            // refresh the timeout if used in forwarding
            if (b_link_used_in_forwarding)
                entry.timeout = DV_entry_t::DV_TIMEOUT;

            /* link hasn't been created or is currently broken (infinite cost),
              or the routing path is the link itself and the cost changed (up or down) */
            if ( entry.cost > cost ||
                (b_link_used_in_forwarding && entry.cost != cost) ) {
                /* if the situation is a broken link reviving, force to
                  send all entries instead of merely the changed ones,
                  because it's possible that the current router has lost all the links,
                  it will need all the information to reconstruct the routing table */
                //if (!b_link_used_in_forwarding || old_cost == INFINITY_COST)
                /*if (old_cost > cost)
                    b_send_incremental_DV = false;*/

                entry.cost = cost;
                entry.timeout = DV_entry_t::DV_TIMEOUT;
                forwarding_table[p->src_id] = port_from;

                /* if we have done the first DV refresh, only do incremental change afterwards.
                  otherwise, send the entire DV table as the update messages */
                //if (b_send_incremental_DV) {
                if (old_cost < cost) {
                    size = packet_t::PACKET_HEADER_SIZE + 4;
                    for (unsigned short port = 0; port < num_ports; ++port)
                    /**
                    Actually, I think it's better to skip the entry rather than
                     sending an entry with infinity cost. But this may result in
                     different behavior as the test example.
                    // poison reverse, avoid using current node as next hop
                    if (forwarding_table[p->src_id] != port)
                    **/
                    if (port_status[port].cost != INFINITY_COST) {
                        packet_t *q = (packet_t*)malloc(size);//new char[size];
                        if (!q) break;  ///***ERROR
                        q->type = DV;
                        q->size = htons(size);
                        q->src_id = router_id;  // router_id is already in network order
                        q->dst_id = port_status[port].id;
                        q->DV_data[0].node_id = p->src_id;
                        //q->DV_data[0].cost = cost;
                        // poison reverse
                        q->DV_data[0].cost = htons(forwarding_table[p->src_id] == port ?
                            INFINITY_COST : cost);

                        sys->send(port, q, size);
                    }
                }else {
                    // send all entries
                    send_DV_update();
                }
            }
        }else /* if (protocal_type == P_LS)*/ {
            LS_entry_t &entry = LS_nodes[router_id];
            entry.neighbors[p->src_id] = cost;
            //src.port = port_from;
            //entry.neighbors[p->src_id].port = port_from;

            if (b_link_used_in_forwarding)
                entry.timeout = LS_entry_t::LS_TIMEOUT;

            // The link cost has changed, compute new routes and notify other nodes.
            if (old_cost != cost) {
                LS_SSSP();
                send_LS_update();
            }
        }//endif protocal type
    } break;

    case DV: {
        unsigned int cost = port_status[port_from].cost;
        // should not happen, inconsistency between port status and actual router id...
        /*if (port_status[port_from].id != p->src_id) {
            printf("ERROR\n");
            break;
        }*/
        bool b_send_all_this_time = false;
        int n_changed = 0;
        for (map <id_netorder_t, DV_entry_t>::iterator it = DV_table.begin();
             it != DV_table.end(); ++it
            ) it->second.changed = false;

        for (int i = 0; i < (size - packet_t::PACKET_HEADER_SIZE) / 4; ++i)
            /* if the destination is the current node,
              check if the direct link cost the least */
            //if (p->DV_data[i].node_id == router_id) {
                //if (cost < )
            //} /* otherwise, do normal 'relax' using triangular inequality */
            //else {
            if (p->DV_data[i].node_id != router_id) {
                DV_entry_t &entry = DV_table[p->DV_data[i].node_id];
                // consider the DV entry refreshed, reset the expiration time
                entry.timeout = DV_entry_t::DV_TIMEOUT;

                unsigned int new_cost = cost + ntohs(p->DV_data[i].cost);
                if (new_cost > INFINITY_COST) new_cost = INFINITY_COST;
                // The next hop is the same, check if the cost has changed
                if (port_from == forwarding_table[p->DV_data[i].node_id]) {
                    // The link cost may either increase or decrease.
                    if (entry.cost != new_cost) {
                        // cost increased, check if there's a direct link that costs smaller
                        if (entry.cost < new_cost) {
                            vector<port_status_t>::iterator it =
                                find(port_status.begin(), port_status.end(), p->DV_data[i].node_id);
                            if (it != port_status.end() && it->cost <= new_cost) {
                                forwarding_table[p->DV_data[i].node_id] = it - port_status.begin();
                                // although with new path, cost not changed
                                //if (it->cost == entry.cost)
                                //    continue;
                                new_cost = it->cost;
                                /* need to inform it of all possible dst nodes,
                                  so force to send all entries. */
                                //b_send_incremental_DV = false;
                                b_send_all_this_time = true;
                            }
                        }
                        entry.cost = new_cost;
                        if (entry.changed == false) n_changed++;
                        entry.changed = true;
                    }
                } // in the else case, normal Bellman-Ford shortest path update
                else if (entry.cost > new_cost) {
                    entry.cost = new_cost;
                    if (entry.changed == false) n_changed++;
                    entry.changed = true;
                    forwarding_table[p->DV_data[i].node_id] = port_from;
                }
            }
        /* If the DV table entries have changed, spread this change to neighbor nodes.
          Only send the CHANGED part incrementally. This is a small optimazation
          of the original version of DV protocal implementation. */
        if (n_changed) {
            if (!b_send_all_this_time)
                send_DV_update_changed_only(n_changed);
            else
                send_DV_update();
        }
    } break;

    case LS: {
        LS_entry_t &entry = LS_nodes[p->src_id];
        entry.timeout = LS_entry_t::LS_TIMEOUT;

        // Skip the packet if the sequence number is smaller than that recorded
        unsigned int seq_num = ntohl(p->sequence_number);
        if (seq_num <= entry.sequence_number)
            break;
        entry.sequence_number = seq_num;

        bool b_changed = false;
        // update the link state of node 'p->src_id'
        size_t n = (size - packet_t::PACKET_HEADER_SIZE - 4) / 4;
        /* Neighbor count reduced, some links died. We have to
          Clean records related to those links now. */
        if (n < entry.neighbors.size()) {
            //entry.neighbors.clear();
            b_changed = true;
            for (map<id_netorder_t, unsigned int>::iterator it = entry.neighbors.begin();
                    it != entry.neighbors.end(); ++it)
                it->second = INFINITY_COST;
            for (size_t i = 0; i < n; ++i) {
                unsigned int &cost = entry.neighbors[p->LS_data[i].neighbor_id];
                cost = ntohs(p->LS_data[i].cost);
            }
            for (map<id_netorder_t, unsigned int>::iterator it = entry.neighbors.begin();
                    it != entry.neighbors.end(); )
                if (it->second == INFINITY_COST) {
                    id_netorder_t id = it->first;
                    // remove LS table entry
                    entry.neighbors.erase(it++);
                    LS_iterator it2 = LS_nodes.find(id);
                    it2->second.neighbors.erase(p->src_id);
                    if (it2->second.neighbors.empty()) {
                        // remove nodes from the graph
                        LS_nodes.erase(it2);
                        // remove forwarding table entry
                        map <id_netorder_t, unsigned short>::iterator it_fw =
                            forwarding_table.find(id);
                        if (it_fw != forwarding_table.end())
                            forwarding_table.erase(it_fw);
                    }
                    /*for (LS_iterator it2 = LS_nodes.begin(); it2 != LS_nodes.end(); ++it2)
                        if (it2->first != p->src_id)
                            it2->second.neighbors.erase(id);*/
                }else {
                    ++it;
                }
        }else {
            // Update the link state data
            for (size_t i = 0; i < n; ++i) {
                unsigned int &cost = entry.neighbors[p->LS_data[i].neighbor_id];
                if (cost != ntohs(p->LS_data[i].cost)) {
                    cost = ntohs(p->LS_data[i].cost);
                    b_changed = true;
                }
            }
        }
        if (b_changed) {
            // re-run the shortest path algorithm
            LS_SSSP();
        }
        // flood the LS update packet to all ports (except port_from)
        int n_sent = 0;
        for (unsigned int port = 0; port < num_ports; ++port)
            if (port != port_from && port_status[port].cost != INFINITY_COST) {
                if (n_sent++ == 0) {
                    sys->send(port, p, size);
                }else {
                    packet_t *q = (packet_t*)malloc(size);//new char[size];
                    if (!q) break;  ///***ERROR
                    memcpy(q, p, size);
                    sys->send(port, q, size);
                }
            }
        // should break from this switch clause and free the memory if nothing is sent
        if (n_sent) return;
    } break;
    default:    // unknown packet type
        break;
    }
    // release the memory
    free(p);//delete p;
}

void RoutingProtocolImpl::send_DV_update() {
    size_t size = packet_t::PACKET_HEADER_SIZE + DV_table.size() * 4;
    for (unsigned short port = 0; port < num_ports; ++port)
        // if the link is not broken
        if (port_status[port].cost != INFINITY_COST) {
            packet_t *q = (packet_t*)malloc(size);//new char[size];
            if (!q) break;  ///***ERROR
            q->type = DV;
            q->size = htons(size);
            q->src_id = router_id;  // router_id is already in network order
            q->dst_id = port_status[port].id;

            int i = 0;
            for (map <id_netorder_t, DV_entry_t>::iterator it = DV_table.begin();
                 it != DV_table.end(); ++it, ++i
                ) {
                q->DV_data[i].node_id = it->first;
                // poison reverse
                q->DV_data[i].cost = htons(forwarding_table[it->first] == port ?
                    INFINITY_COST : it->second.cost);
            }
            sys->send(port, q, size);
        }
}

// send only the changed part of DV table
void RoutingProtocolImpl::send_DV_update_changed_only(int n_changed) {
    // construct the message packet
    int size = packet_t::PACKET_HEADER_SIZE + n_changed * 4;
    for (unsigned short port = 0; port < num_ports; ++port)
        // if the link is not broken
        if (port_status[port].cost != INFINITY_COST) {
            packet_t *q = (packet_t*)malloc(size);//new char[size];
            if (!q) break;  ///***ERROR
            q->type = DV;
            q->size = htons(size);
            q->src_id = router_id;  // router_id is already in network order
            q->dst_id = port_status[port].id;

            int i = 0;
            for (map <id_netorder_t, DV_entry_t>::iterator it = DV_table.begin();
                 it != DV_table.end(); ++it
                ) if (it->second.changed) {
                q->DV_data[i].node_id = it->first;
                // poison reverse
                q->DV_data[i].cost = htons(forwarding_table[it->first] == port ?
                    INFINITY_COST : it->second.cost);
                ++i;
            }
            sys->send(port, q, size);
        }
}

/// Dijkstra's Algorithm ///
void RoutingProtocolImpl::LS_SSSP() {
    // use a heap to maintain the frontier nodes
    priority_queue<LS_item_t, vector<LS_item_t>, LS_comparator> q;
    LS_iterator src = LS_nodes.find(router_id);

    // initialize costs to infinities
    //vector<unsigned int> dist(LS_nodes.size(), INFINITY_COST);
    for (LS_iterator it = LS_nodes.begin(); it != LS_nodes.end(); ++it)
        //if (it->first != router_id) {
            // avoid changing the costs of neighbors'
            //if (src->second.neighbors.find(it->first) == src->second.neighbors.end())
                it->second.cost = INFINITY_COST;
        //}
    src->second.cost = 0;
    q.push(make_pair(0, src));  // add the source node

    // Stop when all the 'n' nodes are added to the shortest path tree.
    for(size_t n = 0; !q.empty() && n < LS_nodes.size(); ++n) {
        LS_iterator v = q.top().second;
        q.pop();
        if (v != src)
            forwarding_table[v->first] = v->second.port;
        // for each neighbor of node 'n', try relaxing the distances.
        for (map<id_netorder_t, unsigned int>::iterator it = v->second.neighbors.begin();
              it != v->second.neighbors.end(); ++it) {
            unsigned int new_cost = v->second.cost + it->second;
            LS_iterator w = LS_nodes.find(it->first);
            // skip if the graph hasn't been contructed completely (some nodes missing)
            if (w == LS_nodes.end()) continue;
            if (w->second.cost > new_cost) {
                w->second.cost = new_cost;
                // use variable 'port' instead of 'parent'
                if (v == src) {
                    w->second.port = find(port_status.begin(), port_status.end(), it->first)
                        - port_status.begin();
                }else {
                    w->second.port = v->second.port;
                }
                //forwarding_table[it->first] = forwarding_table[v->first];
                q.push(make_pair(new_cost, w));
            }
        }
    }
}

void RoutingProtocolImpl::send_LS_update() {
    //if (num_ports == 0) return;

    LS_entry_t &entry = LS_nodes[router_id];
    if (entry.neighbors.empty()) return;
    ++entry.sequence_number;

    size_t size = packet_t::PACKET_HEADER_SIZE+4 + entry.neighbors.size()*4;

    packet_t *p = (packet_t*)malloc(size);//new char[size];
    if (!p) return; ///***ERROR
    p->type = LS;
    p->size = htons(size);
    p->src_id = router_id;
    //p->dst_id = ? // ignored
    p->sequence_number = htonl(entry.sequence_number);
    int i = 0;
    for (map<id_netorder_t, unsigned int>::iterator it = entry.neighbors.begin();
         it != entry.neighbors.end(); ++it, ++i) {
        p->LS_data[i].neighbor_id = it->first;
        p->LS_data[i].cost = htons(it->second);
    }

    int n_sent = 0;
    for (unsigned int port = 0; port < num_ports; ++port)
        if (port_status[port].cost != INFINITY_COST) {
            if (n_sent++ > 0) {
                packet_t *q = (packet_t*)malloc(size);//new char[size];
                if (q == 0) break; ///***ERROR
                memcpy(q, p, size);
                sys->send(port, q, size);
            }else {
                sys->send(port, p, size);
            }
        }
    if (n_sent == 0)
        free(p);//delete p;
}
