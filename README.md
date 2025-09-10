# Location-Based_WSN

Description:

This project was developed alongside my research for a thesis paper in Computer Engineering at California State University, Fullerton. The research focused on the design and implementation of a location-based wireless sensor network (WSN).

As part of this thesis, I developed a WSN with environmental monitoring capabilities and an additional feature: node localization within the network. This localization was achieved using single-hop distance measurement through peer-to-peer (P2P) communication between mesh nodes in a Bluetooth Mesh network.

The network allowed selected nodes to determine the distance to other nodes of interest, specifically the sensor node. The system estimated the one-hop distance between nodes using P2P measurements.

Network Architecture & Implementation:

The network structure consisted of six nodes, each with a specific role:

🔹 Client Node (nRF5340 DK) – Initiates distance calculations to the sensor node and has the information returned to it.

🔹 Sensor Node (Nordic Thingy:53) – Collects environmental data and operates in a low-power mode.

🔹 Friend Node (nRF5340 DK) – Supports the low-power sensor node by storing messages while the sensor sleeps.

🔹 Anchor Nodes (Three nRF5340 DKs) – Act as fixed reference points to help calculate distances between the client and sensor nodes.

The network was structured in a linear configuration:

The client node was positioned at one end of the line.

The sensor and friend nodes were at the opposite end.

The anchor nodes were placed between them to enable distance calculations.

This simple yet effective layout allowed for intuitive distance estimation, where the client-to-anchor and anchor-to-sensor measurements were combined to determine the total client-to-sensor distance.

Distance Measurement & Localization:

🔹 Single-Hop Peer-to-Peer (P2P) Measurements – Nodes measured distances between direct neighbors in the Bluetooth Mesh network.

🔹 Anchor-Based Localization – The system estimated relative positioning using anchor nodes as fixed reference points.

🔹 Low-Power Sensor Support – The friend node stored messages for the sensor node while it was in sleep mode, optimizing energy efficiency.

🔹 Environmental Considerations – This method is most accurate in environments where nodes share the same elevation (Z-axis), as it does not account for height differences due to the lack of an antenna matrix.

Additional Resources:
For more details, refer to the thesis report and implementation code in this repository.

Demonstration video comming soon.
