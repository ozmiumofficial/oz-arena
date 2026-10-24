# Q3Remaster dedicated server (MULTIPLAYER_PLAN.md M7): the game run headless on Linux, no window.
# NOT BUILT YET: written on a Windows PC with no Linux or Docker to try it. Expect to fix the first build.
#
#   docker build -f server/Dockerfile -t q3remaster-server .          (from the repo root)
#   docker run -d --name q3 -v /srv/q3/baseq3:/q3/baseq3:ro -v /srv/q3/data:/data \
#       -p 27960:27960/udp q3remaster-server
#
# You supply your own retail pak0.pk3 (and the other paks) in /srv/q3/baseq3. The server's settings are
# /data/server.json (server/server.example.json), its identity is /data/online/server.key.

FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git ca-certificates python3 python3-pip python3-venv unzip curl \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /venv && /venv/bin/pip install scons
WORKDIR /src
COPY tools/build_q3core.py tools/build_q3core.py
COPY tools/q3core tools/q3core
COPY tools/godot_template tools/godot_template
COPY godot godot
# fetches the pinned ioquake3 + godot-cpp, applies the patches, builds libq3core + qagamex86_64 for Linux
RUN /venv/bin/python tools/build_q3core.py --platform linux --release --jobs 4
RUN curl -fsSL -o /tmp/godot.zip \
      https://github.com/godotengine/godot/releases/download/4.7.2-stable/Godot_v4.7.2-stable_linux.x86_64.zip \
    && unzip -q /tmp/godot.zip -d /tmp && mv /tmp/Godot_v4.7.2-stable_linux.x86_64 /usr/local/bin/godot

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libfontconfig1 libx11-6 libxcursor1 \
    libxi6 libxinerama1 libxrandr2 libgl1 libasound2t64 && rm -rf /var/lib/apt/lists/*
COPY --from=build /usr/local/bin/godot /usr/local/bin/godot
COPY --from=build /src/godot /q3/game
ENV Q3_BASEQ3=/q3/baseq3 XDG_DATA_HOME=/data
VOLUME /data
EXPOSE 27960/udp
# first run: imports the project (Godot needs a .godot cache); then the server loop
CMD ["sh", "-c", "cd /q3/game && ([ -d .godot ] || godot --headless --import --quit) && exec godot --headless --path . -- dedicated=/data/server.json"]
