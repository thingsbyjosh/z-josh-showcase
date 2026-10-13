// fixture_shape.mjs -- compares a query payload's SHAPE against a fixture dumped from the real engine.
//
// The fixtures in app/ui-fixtures/ were dumped from the product's own engine, with the overlay as its
// host. The browser host fills the same view structures from an invented world, so VALUES differ by
// design; field names and JSON types must not. A field the fixture has and the payload lacks, or carries
// as a different type, is a defect in the browser host's provider -- the app reads that field.
//
// The rules, each chosen for a case the real fixtures contain:
//   - Objects: every key the fixture has must be present with the same JSON type. Extra keys in the
//     payload are reported, not failed: the serializer emits several keys only when they apply
//     (calendar's gateQual, dlcPack, eventId...), so a fixture never witnesses all of them.
//   - Arrays of objects: the fixture's elements are merged. A key present in EVERY fixture element is
//     required in every payload element; a key present in only some is optional (conditional keys again).
//     Every payload element is checked, not just the first.
//   - A fixture `null` or an empty array on EITHER side witnesses no type. Those paths are returned as
//     `unwitnessed` so the caller can see what was NOT checked, and may supply the expected shape
//     itself through `witness` (a map from path to an example value) -- typically for a nullable object
//     the fixture happened to capture as null.
//   - A payload `null` where the fixture has a value is a defect, unless the path is listed in
//     `nullable` (a field the contract documents as nullable).
//
// Paths are written like `slots[].trackName`; `[]` stands for "every element".

const typeOf = (v) => (v === null ? 'null' : Array.isArray(v) ? 'array' : typeof v);

// The merged expectation for an array of fixture elements: per key, its JSON type and whether every
// element carried it. Only objects merge; an array of scalars keeps its first element's type.
function mergeElements(elements) {
  if (!elements.every((e) => typeOf(e) === 'object')) return { scalar: elements[0] };
  const keys = new Map();
  for (const el of elements) {
    for (const [k, v] of Object.entries(el)) {
      const prev = keys.get(k);
      if (!prev) keys.set(k, { samples: [v], count: 1 });
      else { prev.samples.push(v); prev.count++; }
    }
  }
  const merged = {};
  const optional = new Set();
  for (const [k, { samples, count }] of keys) {
    // prefer a non-null sample, so one null in the fixture does not hide a type another element shows
    merged[k] = samples.find((s) => s !== null) ?? null;
    if (count < elements.length) optional.add(k);
    // an array-valued key: carry every element across fixture elements, so the merge recurses fully
    if (Array.isArray(merged[k])) merged[k] = samples.filter(Array.isArray).flat();
  }
  return { object: merged, optional };
}

export function checkFixtureShape(actual, fixture, { witness = {}, nullable = [] } = {}) {
  const defects = [];
  const extras = [];
  const unwitnessed = [];
  const nullableSet = new Set(nullable);

  function walk(a, f, p) {
    if (f === null || f === undefined) {
      if (p in witness) { walk(a, witness[p], p); return; }
      unwitnessed.push(p);
      return;
    }
    const ta = typeOf(a), tf = typeOf(f);
    if (ta === 'null' && tf !== 'null') {
      if (!nullableSet.has(p)) defects.push(`${p}: null where the fixture has ${tf}`);
      return;
    }
    if (ta !== tf) { defects.push(`${p}: ${ta} where the fixture has ${tf}`); return; }
    if (tf === 'object') {
      for (const k of Object.keys(f)) {
        const kp = p ? `${p}.${k}` : k;
        if (!(k in a)) defects.push(`${kp}: missing`);
        else walk(a[k], f[k], kp);
      }
      for (const k of Object.keys(a)) if (!(k in f)) extras.push(p ? `${p}.${k}` : k);
      return;
    }
    if (tf === 'array') {
      const ep = `${p}[]`;
      // An empty side checks nothing, and says so: an empty payload array is reported even when a
      // witness was supplied, or a pass would read as "the elements were checked" when there were none.
      if (a.length === 0) { unwitnessed.push(`${ep} (payload empty)`); return; }
      if (f.length === 0) {
        if (ep in witness) { for (const el of a) walk(el, witness[ep], ep); }
        else unwitnessed.push(ep);
        return;
      }
      const m = mergeElements(f);
      if ('scalar' in m) { for (const el of a) walk(el, m.scalar, ep); return; }
      // Every payload element against the merged expectation. Paths name the key, not the index, so
      // one defect repeated across 300 slots collapses to a single line in the result.
      for (const el of a) {
        if (typeOf(el) !== 'object') { defects.push(`${ep}: ${typeOf(el)} where the fixture has object`); continue; }
        for (const k of Object.keys(m.object)) {
          const kp = `${ep}.${k}`;
          if (!(k in el)) { if (!m.optional.has(k)) defects.push(`${kp}: missing`); }
          else walk(el[k], m.object[k], kp);
        }
        for (const k of Object.keys(el)) if (!(k in m.object)) extras.push(`${ep}.${k}`);
      }
      return;
    }
    // scalars: the type check above is the whole check
  }

  walk(actual, fixture, '');
  return {
    defects: [...new Set(defects)],
    extras: [...new Set(extras)],
    unwitnessed: [...new Set(unwitnessed)],
  };
}
