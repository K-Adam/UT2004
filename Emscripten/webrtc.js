// The browser side of IpDrv/Src/WebRtcBrowser.cpp, linked with --js-library.

addToLibrary({
  $UTWebRtc: {
    peers: new Map(),
    next: 1,
    // The host socket lives as long as the page, so the id players join by survives map changes.
    host: null,
    accepted: [],
    // Used until the web master's list arrives and when it gives none.
    signaled: { iceServers: [{ urls: 'stun:stun.cloudflare.com:3478' }] },
    relay: null,

    // The web master's STUN and relay servers. One request serves the page's joins and queries, and a failure
    // is kept for a while too, or every server browser query would ask again.
    ice: (url) => {
      let relay = UTWebRtc.relay;
      if (!relay || Date.now() > relay.until) {
        relay = UTWebRtc.relay = { until: Infinity };
        relay.config = fetch(`${url}/ice`, { signal: AbortSignal.timeout(5000) })
          .then(async (res) => {
            if (!res.ok) throw new Error(res.status);
            const config = await res.json();
            // An empty list would be accepted and take the STUN server away.
            if (!config.iceServers?.length) throw new Error('no ICE servers');
            // The credential lasts 48 hours and an expired one drops a relayed game.
            relay.until = Date.now() + 24 * 3600 * 1000;
            return config;
          })
          .catch(() => {
            relay.until = Date.now() + 600 * 1000;
            return UTWebRtc.signaled;
          });
      }
      return relay.config;
    },

    add: (config, maxInbox) => {
      const peer = { inbox: [], closed: false };
      const id = UTWebRtc.next++;
      UTWebRtc.peers.set(id, peer);
      peer.fail = (e) => {
        err(`WebRTC: ${e}`);
        peer.closed = true;
      };
      try {
        peer.pc = new RTCPeerConnection(config);
        // Negotiated as channel 0 on both ends, unordered and unreliable like UDP.
        peer.dc = peer.pc.createDataChannel('', { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
        peer.dc.binaryType = 'arraybuffer';
        peer.dc.onmessage = (e) => {
          if (peer.inbox.length < maxInbox) peer.inbox.push(new Uint8Array(e.data));
        };
        peer.dc.onclose = () => { peer.closed = true; };
        peer.pc.onconnectionstatechange = () => {
          if (peer.pc.connectionState == 'failed' || peer.pc.connectionState == 'closed') peer.closed = true;
        };
      } catch (e) {
        peer.fail(e);
      }
      return [id, peer];
    },

    // Sends what was gathered after 3 seconds at the latest, the web master gives the host 8 to answer.
    gathered: (pc) => new Promise((resolve) => {
      const done = () => {
        if (pc.iceGatheringState == 'complete') resolve();
      };
      pc.addEventListener('icegatheringstatechange', done);
      setTimeout(resolve, 3000);
      done();
    }),

    close: (id) => {
      UTWebRtc.peers.get(id)?.pc?.close();
      UTWebRtc.peers.delete(id);
    },

    openHost: (url) => {
      const host = UTWebRtc.host;
      const ws = new WebSocket(url.replace(/^http/, 'ws') + '/host');
      host.ws = ws;
      ws.onmessage = async (e) => {
        const m = JSON.parse(e.data);
        if (m.type == 'id') {
          host.id = m.id;
          host.config = m.iceServers ? { iceServers: m.iceServers } : UTWebRtc.signaled;
        } else if (m.type == 'offer' && host.active && host.setup < 16) {
          // Each offer costs a peer connection for the seconds it gathers, so a burst of them goes unanswered.
          host.setup++;
          const [id, peer] = UTWebRtc.add(host.config, host.maxInbox);
          try {
            await peer.pc.setRemoteDescription({ type: 'offer', sdp: m.sdp });
            await peer.pc.setLocalDescription(await peer.pc.createAnswer());
            await UTWebRtc.gathered(peer.pc);
            ws.send(JSON.stringify({ type: 'answer', id: m.id, sdp: peer.pc.localDescription.sdp }));
            UTWebRtc.accepted.push({ id, ip: m.ip });
          } catch (e) {
            peer.fail(e);
            UTWebRtc.close(id);
          } finally {
            host.setup--;
          }
        }
      };
      ws.onclose = () => {
        host.id = '';
        setTimeout(() => UTWebRtc.openHost(url), 10000);
      };
    },
  },

  ut_webrtc_connect__deps: ['$UTWebRtc'],
  ut_webrtc_connect__sig: 'ippi',
  ut_webrtc_connect: (answer, ufrag, maxInbox) => {
    answer = UTF8ToString(answer);
    ufrag = UTF8ToString(ufrag);
    const [id, peer] = UTWebRtc.add(undefined, maxInbox);
    // The server learns the ufrag from the first STUN request, so the offer carries the one its answer was written for.
    peer.pc?.createOffer()
      .then((offer) => peer.pc.setLocalDescription({
        type: 'offer',
        sdp: offer.sdp.replace(/a=ice-ufrag:.*\r\n/g, `a=ice-ufrag:${ufrag}\r\n`).replace(/a=ice-pwd:.*\r\n/g, `a=ice-pwd:${ufrag}\r\n`),
      }))
      .then(() => peer.pc.setRemoteDescription({ type: 'answer', sdp: answer }))
      .catch(peer.fail);
    return id;
  },

  ut_webrtc_connect_signal__deps: ['$UTWebRtc'],
  ut_webrtc_connect_signal__sig: 'ippi',
  ut_webrtc_connect_signal: (url, hostId, maxInbox) => {
    url = UTF8ToString(url);
    hostId = UTF8ToString(hostId);
    const [id, peer] = UTWebRtc.add(UTWebRtc.signaled, maxInbox);
    (async () => {
      const config = await UTWebRtc.ice(url);
      // A list the browser rejects leaves the STUN server the peer was made with.
      try {
        peer.pc.setConfiguration(config);
      } catch (e) {
        err(`WebRTC: ${e}`);
      }
      await peer.pc.setLocalDescription(await peer.pc.createOffer());
      await UTWebRtc.gathered(peer.pc);
      // Sent as text/plain, which needs no CORS preflight.
      const res = await fetch(`${url}/offer`, { method: 'POST', body: JSON.stringify({ host: hostId, sdp: peer.pc.localDescription.sdp }) });
      if (!res.ok) throw new Error(`the host did not answer (${res.status})`);
      await peer.pc.setRemoteDescription({ type: 'answer', sdp: (await res.json()).sdp });
    })().catch(peer.fail);
    return id;
  },

  ut_webrtc_host__deps: ['$UTWebRtc'],
  ut_webrtc_host__sig: 'vpi',
  ut_webrtc_host: (url, maxInbox) => {
    if (!UTWebRtc.host) {
      UTWebRtc.host = { id: '', maxInbox, setup: 0 };
      UTWebRtc.openHost(UTF8ToString(url));
    }
    UTWebRtc.host.active = true;
  },

  // Between hosted games the socket stays open to keep the id, but offers go unanswered.
  ut_webrtc_host_stop__deps: ['$UTWebRtc'],
  ut_webrtc_host_stop__sig: 'v',
  ut_webrtc_host_stop: () => {
    if (!UTWebRtc.host) return;
    UTWebRtc.host.active = false;
    for (const { id } of UTWebRtc.accepted.splice(0)) UTWebRtc.close(id);
  },

  // The id players join by, empty while the socket connects.
  ut_webrtc_host_id__deps: ['$UTWebRtc', '$stringToUTF8'],
  ut_webrtc_host_id__sig: 'ipi',
  ut_webrtc_host_id: (buf, max) => stringToUTF8(UTWebRtc.host?.id ?? '', buf, max),

  ut_webrtc_host_state__deps: ['$UTWebRtc'],
  ut_webrtc_host_state__sig: 'vp',
  ut_webrtc_host_state: (json) => {
    const ws = UTWebRtc.host?.ws;
    if (ws?.readyState == WebSocket.OPEN) ws.send(`{"type":"state","server":${UTF8ToString(json)}}`);
  },

  ut_webrtc_accept__deps: ['$UTWebRtc', '$stringToUTF8'],
  ut_webrtc_accept__sig: 'ipi',
  ut_webrtc_accept: (ip, max) => {
    const next = UTWebRtc.accepted.shift();
    if (!next) return 0;
    stringToUTF8(next.ip ?? '', ip, max);
    return next.id;
  },

  ut_webrtc_send__deps: ['$UTWebRtc'],
  ut_webrtc_send__sig: 'iipi',
  ut_webrtc_send: (id, data, count) => {
    const peer = UTWebRtc.peers.get(id);
    if (peer?.dc?.readyState != 'open') return 0;
    peer.dc.send(HEAPU8.slice(data, data + count));
    return 1;
  },

  ut_webrtc_recv__deps: ['$UTWebRtc'],
  ut_webrtc_recv__sig: 'iipi',
  ut_webrtc_recv: (id, data, max) => {
    const peer = UTWebRtc.peers.get(id);
    if (!peer) return -1;
    let msg;
    // Larger than any game packet, so not from the game.
    while ((msg = peer.inbox.shift()) && msg.length > max) {}
    if (!msg) return peer.closed ? -1 : 0;
    HEAPU8.set(msg, data);
    return msg.length;
  },

  ut_webrtc_close__deps: ['$UTWebRtc'],
  ut_webrtc_close__sig: 'vi',
  ut_webrtc_close: (id) => UTWebRtc.close(id),
});
