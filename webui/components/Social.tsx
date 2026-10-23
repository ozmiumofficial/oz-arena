"use client";
// Friends and parties in the main menu: the top bar's status pill, party strip and friends button,
// and the Friends panel that slides in from the right (F, the friends button, or Y on a controller).
// Godot's Online autoload holds the connection; everything here is drawn from its "social" messages.
import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu, setSocialOpen, toast } from "@/lib/store";
import { useLayer } from "@/lib/nav";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { nameRuns, plainName, social, statusLine, type Friend, type Person, type Social } from "@/lib/social";
import BannerText from "./BannerText";
import Button from "./controls/Button";
import { Badge, EmptyState, SectionTitle } from "./PagePanel";

// ------------------------------------------------------------------ small pieces

export function Q3Name({ name, className }: { name: string; className?: string }) {
  return (
    <span className={className}>
      {nameRuns(name).map((r, i) => (
        <span key={i} style={r.color ? { color: r.color } : undefined}>{r.text}</span>
      ))}
    </span>
  );
}

function Head({ p, size = "md", presence }: { p: Person; size?: "sm" | "md"; presence?: Friend["status"] }) {
  const [broken, setBroken] = useState(false);
  const dim = size === "sm" ? "h-8 w-8" : "h-11 w-11";
  return (
    <div className={cn("relative shrink-0", dim)}>
      {broken ? (
        <div className={cn("grid place-items-center rounded-sm border-b-2 bg-white/5 font-q3 text-[0.8rem] text-dim", dim)} style={{ borderColor: p.color }}>
          {plainName(p.name).slice(0, 1).toUpperCase()}
        </div>
      ) : (
        // eslint-disable-next-line @next/next/no-img-element
        <img src={`/q3/icons/${p.model}.png`} alt="" onError={() => setBroken(true)} className={cn("rounded-sm border-b-2 object-cover", dim)} style={{ borderColor: p.color }} />
      )}
      {presence && <span className={cn("absolute -bottom-0.5 -right-0.5 h-3 w-3 rounded-full border-2 border-[#0d0a09]", dot(presence))} />}
    </div>
  );
}

function dot(s: Friend["status"]) {
  return s === "match" ? "bg-glow" : s === "editor" ? "bg-amber" : s === "online" ? "bg-[#5ccf4a]" : "bg-faint";
}

// ------------------------------------------------------------------ top bar

const PILL: Record<Social["status"], [string, string]> = {
  online: ["Online", "bg-[#5ccf4a] shadow-[0_0_0.5rem_#5ccf4a]"],
  connecting: ["Connecting", "bg-amber animate-pulse"],
  offline: ["Offline", "bg-glow"],
  disabled: ["Online off", "bg-faint"],
};

export function OnlinePill() {
  const status = useMenu((s) => s.social.status);
  const reason = useMenu((s) => s.social.reason);
  const [label, cls] = PILL[status];
  return (
    <button
      onClick={() => {
        sfx.move();
        setSocialOpen(true);
      }}
      title={reason || label}
      className="flex items-center gap-2 rounded-full border border-white/10 bg-black/45 px-3 py-1 transition-colors hover:border-white/25"
    >
      <span className={cn("h-2 w-2 rounded-full", cls)} />
      <span className="font-q3 text-[0.65rem] tracking-[0.2em] text-dim">{label}</span>
    </button>
  );
}

/** The party's faces, leader first, when you're in one. */
export function PartyStrip() {
  const party = useMenu((s) => s.social.state?.party ?? null);
  return (
    <AnimatePresence>
      {party && (
        <motion.button
          initial={{ opacity: 0, x: 12 }}
          animate={{ opacity: 1, x: 0 }}
          exit={{ opacity: 0, x: 12 }}
          onClick={() => {
            sfx.move();
            setSocialOpen(true);
          }}
          title="Party"
          className="flex items-center gap-2 rounded-sm border border-white/10 bg-black/45 py-1 pl-1 pr-3 transition-colors hover:border-white/25"
        >
          <div className="flex -space-x-1">
            {[...party.members].sort((a, b) => Number(b.leader) - Number(a.leader)).map((m) => (
              <div key={m.id} className={cn("relative rounded-sm ring-2", m.leader ? "z-10 ring-amber/80" : "ring-black/80", !m.online && "opacity-40")}>
                <Head p={m} size="sm" />
              </div>
            ))}
          </div>
          <span className="font-q3 text-[0.65rem] tracking-[0.2em] text-dim">
            Party {party.members.length}/{party.max}
          </span>
        </motion.button>
      )}
    </AnimatePresence>
  );
}

export function FriendsButton() {
  const online = useMenu((s) => s.social.state?.friends.filter((f) => f.online).length ?? 0);
  const waiting = useMenu((s) => (s.social.state?.incoming.length ?? 0) + (s.social.state?.invites.length ?? 0));
  const open = useMenu((s) => s.socialOpen);
  return (
    <button
      onClick={() => {
        sfx.move();
        setSocialOpen(!open);
      }}
      title="Friends (F)"
      className={cn(
        "relative flex h-9 items-center gap-2 rounded-full border bg-black/45 px-3 transition-colors",
        open ? "border-glow/60 text-ink-hot" : "border-white/10 text-dim hover:border-white/25",
      )}
    >
      <svg viewBox="0 0 24 24" className="h-4 w-4 fill-none stroke-current stroke-2">
        <circle cx="9" cy="8" r="3.5" />
        <path d="M2.5 20c0-3.6 2.9-6.2 6.5-6.2s6.5 2.6 6.5 6.2M16 4.8a3.4 3.4 0 0 1 0 6.4M18 13.9c2.1.7 3.5 2.9 3.5 6.1" />
      </svg>
      <span className="font-q3 text-[0.7rem] tracking-[0.15em]">{online}</span>
      {waiting > 0 && (
        <span className="absolute -right-1 -top-1 grid h-4 min-w-4 place-items-center rounded-full bg-red px-1 font-q3 text-[0.55rem] text-ink-hot shadow-[0_0_0.6rem_rgb(219_36_16/0.8)]">
          {waiting}
        </span>
      )}
    </button>
  );
}

// ------------------------------------------------------------------ the panel

export function SocialPanel() {
  const open = useMenu((s) => s.socialOpen);
  return <AnimatePresence>{open && <Panel key="social" />}</AnimatePresence>;
}

function Panel() {
  const ref = useRef<HTMLDivElement>(null);
  const sc = useMenu((s) => s.social);
  const close = () => {
    sfx.out();
    setSocialOpen(false);
  };
  useLayer(ref, { onBack: close });
  const st = sc.state;
  return (
    <motion.aside
      ref={ref}
      initial={{ opacity: 0, x: "3rem", filter: "blur(6px)" }}
      animate={{ opacity: 1, x: 0, filter: "blur(0px)" }}
      exit={{ opacity: 0, x: "2rem", filter: "blur(4px)" }}
      transition={{ duration: 0.3, ease: [0.2, 0.9, 0.1, 1] }}
      className="q3-grain absolute bottom-[6.5rem] right-[4.5rem] top-[6.5rem] z-50 flex w-[31rem] flex-col overflow-hidden rounded-sm border border-white/[0.08] bg-glass shadow-[0_2rem_6rem_rgb(0_0_0/0.8)] backdrop-blur-md"
    >
      <div className="q3-corners pointer-events-none absolute inset-0" />
      <motion.div className="absolute left-0 right-0 top-0 h-[2px] origin-left bg-gradient-to-r from-glow via-red to-transparent" initial={{ scaleX: 0 }} animate={{ scaleX: 1 }} transition={{ duration: 0.6, delay: 0.1 }} />
      <header className="flex items-end justify-between px-7 pb-3 pt-6">
        <BannerText text="Friends" height={2.1} />
        <button onClick={close} className="mb-1 flex items-center gap-2 font-q3 text-[0.7rem] tracking-[0.2em] text-dim transition-colors hover:text-ink-hot">
          <kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.6rem]">ESC</kbd> CLOSE
        </button>
      </header>
      {st && <MyCode code={st.me.code} />}
      <div className="mx-7 h-px bg-gradient-to-r from-white/15 via-white/5 to-transparent" />
      <div className="min-h-0 flex-1 overflow-y-auto px-7 pb-6 pt-4">
        <Connection sc={sc} />
        {st && (
          <>
            <AddFriend />
            <Invites sc={sc} />
            <PartySection sc={sc} />
            <FriendsList sc={sc} />
          </>
        )}
      </div>
    </motion.aside>
  );
}

function MyCode({ code }: { code: string }) {
  return (
    <div data-row className="flex items-center gap-3 px-7 pb-4">
      <span className="font-q3 text-[0.65rem] tracking-[0.2em] text-dim">Your friend code</span>
      <span className="rounded-sm border border-white/10 bg-black/50 px-2.5 py-1 font-mono text-[1rem] tracking-[0.18em] text-ink-hot select-text">{code}</span>
      <Button small onClick={() => (social("copy_code"), toast("Friend code copied."))}>Copy</Button>
    </div>
  );
}

function Connection({ sc }: { sc: Social }) {
  if (sc.status === "online" && sc.state) return null;
  const text =
    sc.status === "disabled"
      ? "Online services are off. Turn them on in Settings, Network."
      : sc.status === "connecting"
        ? "Connecting to online services…"
        : `Offline. ${sc.reason || "Can't reach the online service."} Retrying.`;
  return (
    <div className="mb-5 flex items-center gap-4 rounded-sm border border-white/10 bg-black/30 px-4 py-3">
      <span className={cn("h-2.5 w-2.5 shrink-0 rounded-full", PILL[sc.status][1])} />
      <p className="flex-1 text-[0.95rem] text-dim">{text}</p>
      {sc.status === "offline" && <Button small onClick={() => social("retry")}>Retry</Button>}
    </div>
  );
}

function AddFriend() {
  const [code, setCode] = useState("");
  const submit = () => {
    const c = code.trim();
    if (!c) return sfx.buzz();
    social("friend_add", { code: c });
    setCode("");
  };
  return (
    <div data-row className="mb-2 flex gap-2">
      <input
        data-nav
        value={code}
        maxLength={12}
        placeholder="Add a friend: ABCD-1234"
        spellCheck={false}
        onChange={(e) => setCode(e.target.value.toUpperCase())}
        onKeyDown={(e) => e.key === "Enter" && submit()}
        className="h-10 min-w-0 flex-1 rounded-sm border border-white/10 bg-black/50 px-3 font-mono text-[1rem] tracking-[0.12em] text-ink-hot placeholder:font-body placeholder:tracking-normal placeholder:text-faint focus:border-glow/70 focus:bg-black/70"
      />
      <Button small onClick={submit}>Add</Button>
    </div>
  );
}

function Invites({ sc }: { sc: Social }) {
  const st = sc.state!;
  if (!st.invites.length && !st.incoming.length && !st.outgoing.length) return null;
  return (
    <>
      <SectionTitle className="mt-6">Waiting on you</SectionTitle>
      <div className="flex flex-col gap-1.5">
        {st.invites.map((inv) => (
          <Row key={"p" + inv.party} p={inv.from} title={<Q3Name name={inv.from.name} />} sub={`Party invite, ${inv.size} in the party`}>
            <Button small primary onClick={() => social("party_accept", { party: inv.party })}>Join</Button>
            <Button small onClick={() => social("party_decline", { party: inv.party })}>Decline</Button>
          </Row>
        ))}
        {st.incoming.map((r) => (
          <Row key={"i" + r.id} p={r} title={<Q3Name name={r.name} />} sub="Wants to be your friend">
            <Button small primary onClick={() => social("friend_accept", { id: r.id })}>Accept</Button>
            <Button small onClick={() => social("friend_decline", { id: r.id })}>Decline</Button>
          </Row>
        ))}
        {st.outgoing.map((r) => (
          <Row key={"o" + r.id} p={r} title={<Q3Name name={r.name} />} sub="Friend request sent" dim>
            <Button small onClick={() => social("friend_cancel", { id: r.id })}>Cancel</Button>
          </Row>
        ))}
      </div>
    </>
  );
}

function PartySection({ sc }: { sc: Social }) {
  const st = sc.state!;
  const party = st.party;
  if (!party) return null;
  const lead = party.leader === st.me.id;
  return (
    <>
      <SectionTitle className="mt-6">
        Party {party.members.length}/{party.max}
      </SectionTitle>
      <div className="flex flex-col gap-1.5">
        {party.members.map((m) => (
          <Row
            key={m.id}
            p={m}
            presence={m.status}
            title={
              <span className="flex items-center gap-2">
                <Q3Name name={m.name} />
                {m.leader && <Badge>Leader</Badge>}
                {m.id === st.me.id && <Badge tone="dim">You</Badge>}
              </span>
            }
            sub={statusLine(m)}
            dim={!m.online}
          >
            {lead && m.id !== st.me.id && (
              <>
                <Button small onClick={() => social("party_promote", { id: m.id })}>Lead</Button>
                <Confirm label="Kick" onYes={() => social("party_kick", { id: m.id })} />
              </>
            )}
          </Row>
        ))}
        {party.pending.map((p) => (
          <Row key={"w" + p.id} p={p} title={<Q3Name name={p.name} />} sub="Invited, waiting…" dim />
        ))}
      </div>
      <PartyChat sc={sc} />
      <div data-row className="mt-3 flex justify-end">
        <Confirm label="Leave party" onYes={() => social("party_leave")} />
      </div>
    </>
  );
}

function PartyChat({ sc }: { sc: Social }) {
  const [text, setText] = useState("");
  const log = useRef<HTMLDivElement>(null);
  useEffect(() => {
    log.current?.scrollTo({ top: log.current.scrollHeight });
  }, [sc.chat.length]);
  const say = () => {
    const t = text.trim();
    if (!t) return;
    social("party_chat", { text: t });
    setText("");
  };
  return (
    <div className="mt-3 rounded-sm border border-white/[0.07] bg-black/30">
      <div ref={log} className="max-h-[8.5rem] min-h-[3rem] overflow-y-auto px-3 py-2 text-[0.92rem] leading-snug">
        {sc.chat.length === 0 && <div className="text-faint">Party chat is empty.</div>}
        {sc.chat.map((l, i) => (
          <div key={i} className="select-text">
            <Q3Name name={l.from.name} className="font-semibold text-ink-hot" />
            <span className="text-dim">: </span>
            <span className="text-ink">{l.text}</span>
          </div>
        ))}
      </div>
      <input
        data-nav
        value={text}
        maxLength={200}
        placeholder="Say something to the party"
        spellCheck={false}
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => e.key === "Enter" && say()}
        className="h-9 w-full border-t border-white/[0.07] bg-transparent px-3 text-[0.95rem] text-ink-hot placeholder:text-faint focus:bg-black/40"
      />
    </div>
  );
}

const GROUPS: [string, (f: Friend) => boolean][] = [
  ["In a match", (f) => f.online && f.status === "match"],
  ["Online", (f) => f.online && f.status !== "match"],
  ["Offline", (f) => !f.online],
];

function FriendsList({ sc }: { sc: Social }) {
  const st = sc.state!;
  const party = st.party;
  const inParty = new Set(party?.members.map((m) => m.id) ?? []);
  const invited = new Set(party?.pending.map((m) => m.id) ?? []);
  const full = !!party && party.members.length >= party.max;
  const byName = (a: Friend, b: Friend) => plainName(a.name).localeCompare(plainName(b.name));
  if (!st.friends.length)
    return (
      <div className="mt-6">
        <EmptyState title="No friends yet" text="Share your friend code, or type a friend's code above." />
      </div>
    );
  return (
    <>
      {GROUPS.map(([label, test]) => {
        const list = st.friends.filter(test).sort(label === "Offline" ? (a, b) => b.seen - a.seen : byName);
        if (!list.length) return null;
        return (
          <div key={label}>
            <SectionTitle className="mt-6">
              {label} <span className="text-dim">{list.length}</span>
            </SectionTitle>
            <div className="flex flex-col gap-1.5">
              {list.map((f) => (
                <Row key={f.id} p={f} presence={f.status} title={<Q3Name name={f.name} />} sub={statusLine(f)} dim={!f.online}>
                  {f.online && !inParty.has(f.id) && (
                    <Button
                      small
                      refuse={invited.has(f.id) ? "Already invited." : full ? "The party is full." : undefined}
                      onRefuse={(why) => toast(why, "warn")}
                      onClick={() => social("party_invite", { id: f.id })}
                    >
                      Invite
                    </Button>
                  )}
                  {f.join && (
                    <Button small refuse="Joining a friend's match arrives with multiplayer." onRefuse={(why) => toast(why, "warn")}>
                      Join
                    </Button>
                  )}
                  <Confirm label="Remove" onYes={() => social("friend_remove", { id: f.id })} />
                </Row>
              ))}
            </div>
          </div>
        );
      })}
      <p className="mt-6 text-center text-[0.8rem] text-faint">
        {st.friends.length}/{st.limits.friends} friends
      </p>
    </>
  );
}

function Row({ p, title, sub, presence, dim, children }: { p: Person; title: React.ReactNode; sub: string; presence?: Friend["status"]; dim?: boolean; children?: React.ReactNode }) {
  return (
    <div data-row className={cn("group flex items-center gap-3 rounded-sm border border-transparent bg-white/[0.025] px-2.5 py-2 transition-colors focus-within:border-white/10 focus-within:bg-white/[0.05] hover:bg-white/[0.05]", dim && "opacity-60 focus-within:opacity-100 hover:opacity-100")}>
      <Head p={p} presence={presence} />
      <div className="min-w-0 flex-1 leading-tight">
        <div className="truncate text-[1rem] text-ink-hot">{title}</div>
        <div className="truncate text-[0.82rem] text-dim">{sub}</div>
      </div>
      {children && <div className="flex shrink-0 gap-1.5">{children}</div>}
    </div>
  );
}

/** A destructive button that asks once: the first press turns it into "Sure?", the second does it. */
function Confirm({ label, onYes }: { label: string; onYes: () => void }) {
  const [armed, setArmed] = useState(false);
  useEffect(() => {
    if (!armed) return;
    const t = setTimeout(() => setArmed(false), 3000);
    return () => clearTimeout(t);
  }, [armed]);
  return (
    <Button small className={cn(armed && "border-glow/70 bg-red/25 text-ink-hot")} onClick={() => (armed ? (setArmed(false), onYes()) : setArmed(true))}>
      {armed ? "Sure?" : label}
    </Button>
  );
}
