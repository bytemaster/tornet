Overview
----------------------------------

This library provides the foundation of a P2P framework based upon UDP
message passing and remote procedure calls.  A UDT like protocol is layered
on top of UDP to provide flow control and retransmitting.  

What makes tornet different from other P2P systems is the utilization of
a digital currency to track contributions to the network and allow anonymous
users to purchase service.

All traffic is encrypted to prevent deep packet insepction.

This software is still under heavy development.


Chunk Lookup
----------------------------------

Each node specifies a certain amount of 'upload bandwidth' that they desire to
sell to the network.  Combined with this target bandwidth, there is also
a target for disk usage.

Each chunk has two properties, distance and popularity.

Each node will determine how 'far' (range) it will serve chunks for and
also the 'min popularity' of the chunks it will serve.

A node with small storage (100MB) and large bandwidth will want to increase its range until the
sum of popularity (frequency) the top 100MB of the most popular items equals the available bandwidth.


Server Rules
-----------------------------------
Expand chunk range until desired bandwidth is consumed.
If disk space runs out, drop lest frequently accessed data, preferring most popular data.


