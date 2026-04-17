# `docs/`

Paper content published as a [Zensical](https://zensical.io) documentation site at:  
**<https://hi-idn.github.io/groundfish-singlevessel-routing/>**

Site structure is defined in [`zensical.toml`](../zensical.toml) at the repo root.

## Structure

| File | Section |
|------|---------|
| `index.md` | Landing page |
| `01-abstract.md` | Abstract |
| `02-introduction.md` | Introduction |
| `03-groundfish-problem.md` | Groundfish Survey Problem |
| `04-capacity-aware-mip.md` | Capacity-Aware MIP formulation |
| `05-matheuristic.md` | Matheuristic framework |
| `06-experimental-setup.md` | Experimental setup |
| `07-results.md` | Results (includes baseline distance tables) |
| `08-discussion-and-conclusion.md` | Discussion and conclusions |
| `paper/` | Reserved for PDF / single-page build output |

## Building the site

```bash
zensical serve    # local preview
zensical build    # write static site to site/
```

The site is deployed to GitHub Pages from the `site/` output directory.

