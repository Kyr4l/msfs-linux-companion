# Guide files

`data/<aircraft>_guide.json` holds the step-by-step guides of one aircraft. A guide is a flowchart: the user picks a
topic ("Start the engines"), the program asks a few questions (answered from the simulator when it can), and the answers
lead through checklists of steps. Each step names the controls to use; the *Guides* tab shows the panel the control is
on with the control highlighted. Run `tools/validate_guides.py` after every edit; it must print no errors.

```jsonc
{
  "aircraft": "Asobo_CJ4",                 // the catalog id (data/<name>_catalog.json "aircraft")
  "title": "Cessna Citation CJ4",
  "sources": ["where the procedures come from (documents, URLs)"],
  "topics": [
    {"id": "engine_start", "title": "Start the engines", "summary": "one line", "start": "es_ground"}
  ],
  "nodes": {
    "es_ground": {
      "type": "question",
      "title": "Where is the aircraft?",
      "text": "Is the aircraft on the ground?",
      "help": "optional, why it matters",
      "auto": {"code": "(A:SIM ON GROUND, Bool)", "rules": [{"op": "==", "value": 1, "choice": "yes"}, {"op": "==", "value": 0, "choice": "no"}]},
      "choices": [
        {"id": "yes", "label": "Yes, parked", "next": "es_prep"},
        {"id": "no", "label": "No, it is flying", "next": "es_flying"}
      ]
    },
    "es_prep": {
      "type": "steps",
      "title": "Before the start",
      "source": "CJ4_Checklist.xml, page BEFORE STARTING ENGINE",
      "steps": [
        {
          "text": "Set the parking brake.",
          "where": "Pedestal, left of the thrust levers.",
          "why": "Keeps the aircraft still while the engines spool up.",
          "warn": "optional caution",
          "source": "CJ4_Checklist.xml: Parking_Brake_Set",
          "controls": [{"api": "LANDING_GEAR_ParkingBrake"}],
          "check": {"code": "(A:BRAKE PARKING POSITION, Bool)", "op": "==", "value": 1}
        }
      ],
      "next": "es_done"
    },
    "es_flying": {"type": "info", "title": "...", "text": "...", "next": ""},
    "es_done": {"type": "end", "title": "Done", "text": "Both engines are running."}
  }
}
```

* **Node types**: `question` (needs `choices`, at least two; `auto` optional), `steps` (needs `steps` and a `source`;
  `next` empty ends the guide), `info` (text, then `next`), `end`.
* **auto**: what the simulator can tell. `code` is calculator code that starts with `(`: `(A:...)`, `(L:A32NX_NAME, number)` or `input_event` a cockpit input
  event name. `rules` map the reading to a choice id. The user can always override the suggestion.
* **check** on a step: the same probe plus `op` (`==` `!=` `>` `<` `>=` `<=`) and `value`; when the simulator confirms
  it the step gets a tick. A step without a `check` is simply read and ticked by hand. Never invent a variable: use
  one you know exists (the standard simulation variables, the aircraft's own L: variables from its catalog or docs).
* **readouts** on a step: live values shown beside it, `[{"label": "N2 right", "unit": "%", "code": "(A:TURB ENG CORRECTED N2:2, percent)"}]`,
  for steps that say "wait until ...". Calculator code can be RPN: `(A:GENERAL ENG COMBUSTION:1, Bool) (A:GENERAL ENG COMBUSTION:2, Bool) +`
  reads 2 when both engines run.
* **controls**: `api` is the `api` field of an entry of the aircraft's catalog (and `function` disambiguates when two
  entries share the api, e.g. the up / down click of one knob). Every control must resolve to exactly one entry.
* **source** is mandatory on every `steps` node and every step: a document, a page, a checklist item. Procedures are
  never written from memory. A step that is a judgement call or a technique rather than a documented procedure says so
  in `why` ("technique, not a checklist item").
* Wording: short, imperative, in the order of the real procedure; one action per step; say where the control is when the
  docs say so, and nothing more.
