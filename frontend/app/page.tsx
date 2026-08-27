"use client";

import Link from "next/link";
import { useEffect, useRef } from "react";
import { animate, stagger } from "animejs";
import Navbar from "@/components/Navbar";

const HERO_VIDEO =
  "https://designerstephen.github.io/public-assets/videos/serene-art-hero.mp4";

// Source: iitmandi.co.in — Student Gymkhana / Technical. Names verbatim; no
// invented descriptions, the source gives none beyond these expansions.
const CLUBS = [
  "Robotronics Club",
  "Space Technology and Astronomy Cell (STAC)",
  "Yantrik Club",
  "KamandPrompt — Programming Club",
  "Nirmaan Club",
  "SAE Collegiate",
  "Entrepreneurship Cell",
  "Kamand BioEngineering Group",
];

const EVENTS = ["Inter IIT Tech Meet", "Avishkar", "Utkarsh"];

// The three gates a retrieval actually passes, in order. Kept in sync with the
// auth flow in README.md — deliberately no step counts or timings that would
// go stale, and nothing here claims a number the page cannot verify.
const STEPS = [
  {
    label: "Sign in",
    title: "Password, then authenticator",
    body: "Your SnTC credentials get you a challenge, not a session. A six-digit code from Google Authenticator finishes the job — so a leaked password on its own opens nothing.",
  },
  {
    label: "Stand at the cabinet",
    title: "Read the code off the enclosure",
    body: "Join the enclosure's Wi-Fi and its portal shows a six-character code that rotates every few minutes. It is only readable within radio range, which is what proves you are physically there.",
  },
  {
    label: "Take your key",
    title: "The rack dispenses your slot",
    body: "Pick your room and the cabinet turns to that slot and releases the key. The retrieval, the return, and anything overdue are all on the record.",
  },
];

/**
 * Fades a set of nodes in, staggered. The hidden state is applied here in JS,
 * not as `opacity: 0` in CSS: if this script never runs the content stays
 * visible instead of the page rendering blank. Costs one frame at full opacity
 * before the effect lands, which the 800ms fade absorbs.
 */
function riseIn(nodes: NodeListOf<Element>, reduced: boolean, delayStep: number) {
  nodes.forEach((n) => {
    (n as HTMLElement).style.opacity = "0";
  });
  return animate(nodes, {
    opacity: [0, 1],
    y: [24, 0],
    duration: reduced ? 0 : 800,
    delay: reduced ? 0 : stagger(delayStep),
    ease: "outQuad",
  });
}

/**
 * Staggers `.reveal` children in the first time the section scrolls into view.
 * Extracted so the council and the steps strip share one observer pattern
 * rather than two near-identical effects drifting apart.
 */
function useRevealOnScroll(
  ref: React.RefObject<HTMLElement>,
  delayStep: number,
) {
  useEffect(() => {
    const root = ref.current;
    if (!root) return;

    // Once per element, not once per effect run. React StrictMode invokes
    // effects twice in dev, which previously started a second `riseIn` over
    // nodes the first was still fading — two anime instances writing the same
    // inline opacity, leaving the heading parked around 0.5 indefinitely. The
    // flag lives on the DOM node so it survives the remount that causes it.
    if (root.dataset.revealed === "1") return;

    const cards = root.querySelectorAll(".reveal");
    if (!cards.length) return;

    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    let running: ReturnType<typeof animate> | null = null;

    const io = new IntersectionObserver(
      (entries) => {
        if (!entries.some((e) => e.isIntersecting)) return;
        io.disconnect(); // fire once
        root.dataset.revealed = "1";
        running = riseIn(cards, reduced, delayStep);
      },
      { threshold: 0.15 },
    );
    io.observe(root);

    return () => {
      io.disconnect();
      // Snap to the finished state rather than revert()-ing a half-played
      // fade: an interrupted revert is what strands a node mid-opacity, and
      // fully-visible is the correct resting state for all of these anyway.
      running?.pause();
      cards.forEach((n) => {
        (n as HTMLElement).style.opacity = "";
      });
    };
  }, [ref, delayStep]);
}

export default function HomePage() {
  const heroRef = useRef<HTMLElement>(null);
  const councilRef = useRef<HTMLElement>(null);
  const howRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const hero = heroRef.current;
    if (!hero) return;

    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    // Scoped to this hero's nodes, not every .hero-rise in the document.
    const entrance = riseIn(hero.querySelectorAll(".hero-rise"), reduced, 200);
    return () => {
      entrance.revert();
    };
  }, []);

  // Both lower sections wait until scrolled into view. IntersectionObserver is
  // native — no scroll library, no listener to throttle.
  useRevealOnScroll(howRef, 120);
  useRevealOnScroll(councilRef, 60);

  return (
    <div className="home-shell">
      <Navbar />

      <main className="hero" ref={heroRef}>
        <video
          className="hero-video"
          src={HERO_VIDEO}
          autoPlay
          muted
          loop
          playsInline
          preload="metadata"
          aria-hidden="true"
          tabIndex={-1}
        />
        <div className="hero-scrim" />

        <div className="hero-inner">
          <p className="hero-eyebrow hero-rise">
            IIT Mandi · Science and Technology Council
          </p>
          <h1 className="hero-title hero-rise">
            Smart <em>Key</em> Storage System
          </h1>
          <p className="hero-lede hero-rise">
            Self-service key retrieval for SAC rooms, built and run by SnTC.
            Password, authenticator code, and physical presence at the enclosure —
            all three, every time.
          </p>
          <div className="hero-actions hero-rise">
            <Link href="/login" className="hero-btn hero-btn--solid">
              Sign In
            </Link>
            {/* Was /keys, which the middleware bounces straight back to /login
                for a signed-out visitor — two buttons, one destination. An
                in-page anchor is the one thing here that works logged out. */}
            <a href="#how" className="hero-btn hero-btn--ghost">
              How It Works
            </a>
          </div>

          <dl className="hero-meta hero-rise">
            <div className="hero-meta-item">
              <dt>Factors to open</dt>
              <dd>3</dd>
            </div>
            <div className="hero-meta-item">
              <dt>Key slots</dt>
              <dd>24</dd>
            </div>
            <div className="hero-meta-item">
              <dt>Self-service</dt>
              <dd>24/7</dd>
            </div>
          </dl>
        </div>
      </main>

      <section className="how" id="how" ref={howRef} aria-labelledby="how-heading">
        <div className="how-inner">
          <p className="how-eyebrow reveal">Getting a key</p>
          <h2 className="how-heading reveal" id="how-heading">
            Three gates, every time
          </h2>
          <p className="how-lede reveal">
            Knowing the password is not enough, and neither is standing next to
            the box. The system asks for both, and writes down what happened.
          </p>

          <ol className="how-steps">
            {STEPS.map((step, i) => (
              <li key={step.label} className="how-step reveal">
                <span className="how-step-num" aria-hidden="true">
                  {String(i + 1).padStart(2, "0")}
                </span>
                <p className="how-step-label">{step.label}</p>
                <h3 className="how-step-title">{step.title}</h3>
                <p className="how-step-body">{step.body}</p>
              </li>
            ))}
          </ol>

          <p className="how-foot reveal">
            Keys are due back after a set window. You get a reminder before the
            deadline, a warning at it, and your coordinator hears about it if the
            key stays out.
          </p>
        </div>
      </section>

      <section className="council" ref={councilRef} aria-labelledby="council-heading">
        <div className="council-inner">
          <p className="council-eyebrow reveal">Student Gymkhana · Technical</p>
          <h2 className="council-heading reveal" id="council-heading">
            Science and Technology Council
          </h2>
          <p className="council-lede reveal">
            SnTC is IIT Mandi&apos;s technical society — a body of students advised
            by faculty. Its major events are the Inter IIT Tech Meet, Avishkar and
            Utkarsh.
          </p>

          <ul className="council-events reveal">
            {EVENTS.map((event) => (
              <li key={event} className="council-event">
                {event}
              </li>
            ))}
          </ul>

          <ul className="club-grid">
            {CLUBS.map((club) => (
              <li key={club} className="club-card reveal">
                {club}
              </li>
            ))}
          </ul>

          <p className="council-note reveal">
            Each club is coordinated by two coordinators and advised by a faculty
            advisor and a co-advisor. All clubs are open to every student at IIT
            Mandi, with a core team formed to run each one.
          </p>
          <p className="council-foot reveal">
            Student Gymkhana, IIT Mandi was established in 2011 and consists of
            eight secretaries organising student activities in their respective
            spheres.
          </p>
        </div>
      </section>
    </div>
  );
}
