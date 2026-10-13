// Tutorial catalog: 18 tours = the shell + 13 tabs + 4 flow screens.
// Each step points at i18n keys (tour.<tour>.<step>.t/.b); anchor = the data-tour value on the
// screen (no anchor = a centered bubble). Steps whose anchor isn't in the DOM are filtered out in
// useTutorial (conditional elements), so a missing anchor degrades gracefully instead of breaking.
// TourKey is defined HERE (doesn't import AppShell's TabKey) so data tests don't drag in
// the entire shell; AppShell assigns its TabKey to TourKey and TypeScript checks the compatibility.

export type TourKey =
  | 'shell'
  | 'hub' | 'calendar' | 'tree' | 'championships' | 'times' | 'market' | 'paddock'
  | 'sponsors' | 'memories' | 'profile' | 'inbox' | 'garage' | 'liveries' | 'settings'
  | 'prerace' | 'results' | 'shop' | 'rollover';

export interface TourStep {
  anchor?: string;
  titleKey: string;
  bodyKey: string;
}

const S = (tour: TourKey, step: string, anchor?: string): TourStep => ({
  anchor,
  titleKey: `tour.${tour}.${step}.t`,
  bodyKey: `tour.${tour}.${step}.b`,
});

export const TOURS: Record<TourKey, TourStep[]> = {
  shell: [
    S('shell', 'intro'),
    S('shell', 'tabs', 'shell.tabs'),
    S('shell', 'credits', 'shell.credits'),
    S('shell', 'rep', 'shell.rep'),
    S('shell', 'lang', 'shell.lang'),
    S('shell', 'win', 'shell.win'),
  ],
  hub: [
    S('hub', 'intro'),
    S('hub', 'card', 'hub.card'),
    S('hub', 'glance', 'hub.glance'),
    S('hub', 'events', 'hub.events'),
    S('hub', 'next', 'hub.next'),
  ],
  calendar: [
    S('calendar', 'intro'),
    S('calendar', 'filters', 'calendar.filters'),
    S('calendar', 'advance', 'calendar.advance'),
    S('calendar', 'list', 'calendar.list'),
  ],
  tree: [
    S('tree', 'intro'),
    S('tree', 'ladder', 'tree.ladder'),
    S('tree', 'matrix', 'tree.matrix'),
  ],
  championships: [
    S('championships', 'intro'),
    S('championships', 'rail', 'champ.rail'),
    S('championships', 'detail', 'champ.detail'),
  ],
  times: [
    S('times', 'intro'),
    S('times', 'list', 'times.list'),
  ],
  market: [
    S('market', 'intro'),
    S('market', 'rep', 'market.rep'),
    S('market', 'offers', 'market.offers'),
  ],
  // the Paddock. A single step: the tab explains itself once you understand the year has
  // three eras and seats are only up for grabs in one of them.
  paddock: [
    S('paddock', 'intro'),
  ],
  sponsors: [
    S('sponsors', 'intro'),
    S('sponsors', 'slots', 'sponsors.slots'),
    S('sponsors', 'manage', 'sponsors.manage'),
  ],
  memories: [
    S('memories', 'intro'),
    S('memories', 'rail', 'memories.rail'),
    S('memories', 'detail', 'memories.detail'),
  ],
  profile: [
    S('profile', 'intro'),
    S('profile', 'hero', 'profile.hero'),
    S('profile', 'arc', 'profile.arc'),
  ],
  inbox: [
    S('inbox', 'intro'),
    S('inbox', 'list', 'inbox.list'),
  ],
  garage: [
    S('garage', 'intro'),
    S('garage', 'grid', 'garage.grid'),
  ],
  liveries: [
    S('liveries', 'intro'),
    S('liveries', 'drop', 'liveries.drop'),
    S('liveries', 'grid', 'liveries.grid'),
  ],
  settings: [
    S('settings', 'intro'),
    S('settings', 'difficulty', 'settings.difficulty'),
    S('settings', 'gameplay', 'settings.gameplay'),
    S('settings', 'replay', 'settings.replay'),
  ],
  prerace: [
    S('prerace', 'intro'),
    S('prerace', 'cars', 'prerace.cars'),
    S('prerace', 'duration', 'prerace.duration'),
    S('prerace', 'actions', 'prerace.actions'),
  ],
  results: [
    S('results', 'intro'),
    S('results', 'hero', 'results.hero'),
    S('results', 'progress', 'results.progress'),
  ],
  shop: [
    S('shop', 'intro'),
    S('shop', 'cards', 'shop.cards'),
    S('shop', 'actions', 'shop.actions'),
  ],
  rollover: [
    S('rollover', 'intro'),
    S('rollover', 'cols', 'rollover.cols'),
    S('rollover', 'contract', 'rollover.contract'),
    S('rollover', 'confirm', 'rollover.confirm'),
  ],
};
