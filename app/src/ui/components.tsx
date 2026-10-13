// Design-system UI primitives (arcade-premium, grid-like). Wrap the classNames
// from design-system.css with types. Screens are built ONLY from these plus utility classes.
import type { CSSProperties, ReactNode, ButtonHTMLAttributes, HTMLAttributes } from 'react';

// Shared <input>/<select> styling for the design system: a SINGLE source (it used to be duplicated in
// Identity.tsx; Calendar.tsx reuses it for its day/month/year date picker). Any screen that
// needs an <input>/<select> with the app's look starts from this object (spread + its own overrides,
// never mutating it).
export const inputStyle: CSSProperties = {
  width: '100%', background: 'var(--surface)', border: '1px solid var(--line)', borderRadius: 4,
  padding: '10px 12px', color: 'var(--text)', fontSize: 14, outline: 'none',
};

export function Panel({ children, style, className = '', ...rest }: { children: ReactNode; style?: CSSProperties; className?: string } & HTMLAttributes<HTMLDivElement>) {
  return <div className={`panel ${className}`.trim()} style={style} {...rest}>{children}</div>;
}

type ChipKind = 'default' | 'accent' | 'gold' | 'green' | 'lock';
export function Chip({ kind = 'default', children, style }: { kind?: ChipKind; children: ReactNode; style?: CSSProperties }) {
  const cls =
    kind === 'accent' ? 'chip chip-accent'
    : kind === 'gold' ? 'chip chip-gold'
    : kind === 'green' ? 'chip chip-green'
    : kind === 'lock' ? 'chip chip-lock'
    : 'chip';
  return <span className={cls} style={style}>{children}</span>;
}

type BtnVariant = 'default' | 'primary' | 'ghost';
type BtnSize = 'default' | 'sm' | 'lg';
export function Btn({
  variant = 'default',
  size = 'default',
  className = '',
  children,
  ...rest
}: { variant?: BtnVariant; size?: BtnSize; className?: string; children: ReactNode } & ButtonHTMLAttributes<HTMLButtonElement>) {
  const v = variant === 'primary' ? ' btn-primary' : variant === 'ghost' ? ' btn-ghost' : '';
  const s = size === 'sm' ? ' btn-sm' : size === 'lg' ? ' btn-lg' : '';
  return <button className={`btn${v}${s} ${className}`.trim()} {...rest}>{children}</button>;
}

export function Kicker({ children, style }: { children: ReactNode; style?: CSSProperties }) {
  return <span className="kicker" style={style}>{children}</span>;
}

// `as`: h1/h2 for the screen title (semantics for screen readers; the styling doesn't change).
export function Display({ children, style, as: Tag = 'span', className }: { children: ReactNode; style?: CSSProperties; as?: 'span' | 'h1' | 'h2'; className?: string }) {
  return <Tag className={className ? `display ${className}` : 'display'} style={style}>{children}</Tag>;
}

export function Mono({ children, style }: { children: ReactNode; style?: CSSProperties }) {
  return <span className="mono" style={style}>{children}</span>;
}

// Minimal SVG icons for the design system (inherit color via currentColor; never emoji in the UI).
export function LockIcon({ size = 9, style }: { size?: number; style?: CSSProperties }) {
  return (
    <svg width={size} height={size} viewBox="0 0 12 12" style={{ flex: 'none', verticalAlign: '-1px', ...style }} aria-hidden="true">
      <rect x="2" y="5" width="8" height="6" rx="1" fill="currentColor" />
      <path d="M4 5.5V3.5a2 2 0 0 1 4 0v2" fill="none" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  );
}

export function BoltIcon({ size = 11, style }: { size?: number; style?: CSSProperties }) {
  return (
    <svg width={size} height={size} viewBox="0 0 12 12" fill="currentColor" style={{ flex: 'none', verticalAlign: '-1px', ...style }} aria-hidden="true">
      <path d="M7 1 2.5 7h3L5 11l4.5-6h-3L7 1Z" />
    </svg>
  );
}

// Discipline/accent dot with a chamfered corner (arbitrary color).
export function AccentDot({ color = 'var(--accent)', size = 10 }: { color?: string; size?: number }) {
  return (
    <span
      style={{
        width: size,
        height: size,
        background: color,
        clipPath: 'polygon(20% 0, 100% 0, 80% 100%, 0 100%)',
        display: 'inline-block',
        flex: 'none',
      }}
    />
  );
}

// Step-based progress indicator (wizard).
export function StepDots({ total, current }: { total: number; current: number }) {
  return (
    <div className="row gap8" style={{ alignItems: 'center' }}>
      {Array.from({ length: total }, (_, i) => (
        <span
          key={i}
          style={{
            width: i === current ? 22 : 8,
            height: 8,
            borderRadius: 2,
            background: i === current ? 'var(--accent)' : i < current ? 'var(--text-3)' : 'var(--line-2)',
            transition: 'all .2s',
          }}
        />
      ))}
    </div>
  );
}
