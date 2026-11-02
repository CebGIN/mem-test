# Vistas previas del diagrama

| Página | Vista previa |
|---|---|
| 1 · Componentes | [01-componentes.png](01-componentes.png) |
| 2 · Clases · Dominio | [02-clases-dominio.png](02-clases-dominio.png) |
| 3 · Clases · Servicios | [03-clases-servicios.png](03-clases-servicios.png) |
| 4 · Secuencia | [04-secuencia.png](04-secuencia.png) |
| 5 · Estados | [05-estados.png](05-estados.png) |
| 6 · Leyenda | [06-leyenda.png](06-leyenda.png) |

Exportar las vistas:

```bash
cd docs/diagramas
for i in 1 2 3 4 5 6; do
  drawio -x -f png -o p$i.png -p $i -s 1.2 ../diagrama.drawio
done
mv p1.png 01-componentes.png
mv p2.png 02-clases-dominio.png
mv p3.png 03-clases-servicios.png
mv p4.png 04-secuencia.png
mv p5.png 05-estados.png
mv p6.png 06-leyenda.png
```