"""Create the academic report using the Codex bundled Python runtime on Windows."""
from pathlib import Path
import re,json
from PIL import Image,ImageDraw,ImageFont
from docx import Document
from docx.shared import Inches,Pt,RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
ROOT=Path(__file__).resolve().parents[1]
EV=ROOT/'artifacts/etapas56-20261029'
assets=ROOT/'docs/etapas56-diagramas';assets.mkdir(exist_ok=True)
fontpath=Path('C:/Windows/Fonts/arial.ttf')
font=ImageFont.truetype(str(fontpath),28)
small=ImageFont.truetype(str(fontpath),24)
def diagram(name,rows):
    # Rows describe actual implementation relationships; no runtime values fabricated.
    im=Image.new('RGB',(1100,130*len(rows)+30),'white');d=ImageDraw.Draw(im)
    for i,labels in enumerate(rows):
        y=20+i*130
        n=len(labels);w=(1060-45*(n-1))//n
        for j,label in enumerate(labels):
            x=20+j*(w+45)
            d.rounded_rectangle((x,y,x+w,y+95),radius=8,outline='#243b53',width=3)
            d.multiline_text((x+w/2,y+47),label,font=font,fill='#172238',anchor='mm',align='center',spacing=5)
            if j<n-1:
                d.line((x+w+6,y+47,x+w+35,y+47),fill='#243b53',width=3)
                d.polygon([(x+w+35,y+47),(x+w+27,y+41),(x+w+27,y+53)],fill='#243b53')
    im.save(assets/name)
diagram('memoria.png',[
 ['Mapa Limine\nsolo USABLE','PMM\nbitmaps y reservas','HHDM\nespacio compartido'],
 ['PMM páginas\n4 KiB','Arenas heap\nkmalloc y kfree','Bloques\nalineados 16 B'],
 ['PMM tramo\n4 páginas','Pila de hilo\n16 KiB','Recolector\ndevuelve al PMM']])
diagram('estados.png',[
 ['NEW\nPCB creado','READY\nen cola','RUNNING\nCPU asignada'],
 ['RUNNING\nthread_yield','READY\nal final FIFO'],
 ['RUNNING\nthread_sleep','WAITING\nfuera de cola','READY\nticks cumplidos'],
 ['RUNNING\nretorno o exit','TERMINATED\nno se reencola','Recolección\ndesde shell']])
diagram('round-robin.png',[
 ['Cola READY\nA B C','Extraer A\ncontext_switch','Hilo A\nejecuta y cede'],
 ['Volver a shell\nA pasa READY','Encolar A\nB C A','Extraer B\nsiguiente despacho']])
diagram('contexto.png',[
 ['Shell\nguarda RSP','NASM\n6 registros + RET','Hilo\npila independiente'],
 ['Hilo cede\nguarda RSP','NASM restaura\nR15 a RBP y RET','Shell\nreencola o recoge']])
doc=Document()
sec=doc.sections[0];sec.page_width=Inches(8.5);sec.page_height=Inches(11)
sec.top_margin=sec.bottom_margin=sec.left_margin=sec.right_margin=Inches(1)
normal=doc.styles['Normal'];normal.font.name='Times New Roman';normal.font.size=Pt(12)
normal.paragraph_format.line_spacing=2
normal.paragraph_format.space_after=Pt(0)
normal.paragraph_format.first_line_indent=Inches(.5)
for name in ('Title','Heading 1','Heading 2'):
    s=doc.styles[name];s.font.name='Times New Roman';s.font.color.rgb=RGBColor(0,0,0)
    s.font.size=Pt(12);s.font.bold=True
    s.paragraph_format.first_line_indent=Inches(0)
    s.paragraph_format.space_before=Pt(12);s.paragraph_format.space_after=Pt(6)
    s.paragraph_format.keep_with_next=True
doc.styles['Heading 1'].paragraph_format.alignment=WD_ALIGN_PARAGRAPH.CENTER
# APA page number in header.
p=sec.header.paragraphs[0];p.alignment=WD_ALIGN_PARAGRAPH.RIGHT
p.paragraph_format.first_line_indent=Inches(0)
fld=OxmlElement('w:fldSimple');fld.set(qn('w:instr'),'PAGE');p._p.append(fld)
doc.core_properties.title='Implementación de un administrador de memoria, procesos, hilos y planificador en ToolchainOS'
doc.core_properties.author='Proyecto ToolchainOS'
title=doc.add_paragraph(doc.core_properties.title.upper(),'Title');title.alignment=WD_ALIGN_PARAGRAPH.CENTER
title.paragraph_format.space_before=Pt(65)
for s in ['Universidad Nacional Hermilio Valdizán','Curso de Sistemas Operativos','Docente Dr. Freddy R. Huapaya C.','Proyecto ToolchainOS','Avance académico V3.1 E56','29 de octubre de 2026']:
    p=doc.add_paragraph(s);p.alignment=WD_ALIGN_PARAGRAPH.CENTER;p.paragraph_format.first_line_indent=Inches(0)
doc.add_page_break()
def picture(path,caption):
    p=doc.add_paragraph();p.paragraph_format.first_line_indent=Inches(0);p.paragraph_format.keep_with_next=True
    p.add_run(caption).bold=True
    p=doc.add_paragraph();p.paragraph_format.first_line_indent=Inches(0)
    p.add_run().add_picture(str(path),width=Inches(6.5))
    p=doc.add_paragraph('Nota. Elaboración a partir del código y los registros de ToolchainOS V3.1 E56.')
    p.paragraph_format.first_line_indent=Inches(0);p.paragraph_format.line_spacing=1;p.runs[0].italic=True
text=(ROOT/'docs/informes/AVANCE_ETAPAS56.md').read_text(encoding='utf-8')
chunks=re.split(r'^## ',text,flags=re.M)[1:]
for chunk in chunks:
    heading,body=chunk.split('\n',1)
    if heading=='Referencias':doc.add_page_break()
    doc.add_heading(heading,1)
    paras=body.strip().split('\n\n')
    if heading=='Referencias':paras.sort(key=str.casefold)
    for para in paras:
        text=para.strip().replace('\n',' ')
        p=doc.add_paragraph()
        titles=['Como construir un sistema operativo','The Limine boot protocol','System V application binary interface AMD64 architecture processor supplement','Código fuente y registros de validación V3.1 E56']
        title=next((x for x in titles if heading=='Referencias' and x in text),None)
        if title:
            a,b=text.split(title,1);p.add_run(a);p.add_run(title).italic=True;p.add_run(b)
        else:p.add_run(text)
    n=heading.split()[0]
    if n=='5':picture(assets/'memoria.png','Figura 1. Relación entre el PMM, HHDM, heap y pilas')
    if n=='7':picture(assets/'estados.png','Figura 2. Transiciones disponibles de PCB y TCB')
    if n=='8':
        picture(assets/'round-robin.png','Figura 3. Cola Round Robin después de una cesión')
        picture(assets/'contexto.png','Figura 4. Secuencia de cambio de contexto cooperativo')
    if heading=='Referencias':
        for p in doc.paragraphs[-4:]:
            p.paragraph_format.left_indent=Inches(.5);p.paragraph_format.first_line_indent=Inches(-.5)
doc.add_page_break()
doc.add_heading('Anexo A Resultados finales reproducibles',1)
results=json.loads((EV/'results.json').read_text())
p=doc.add_paragraph('La ejecución final registra '+str(results['counts']['PASSED'])+' grupos superados, '+str(results['counts']['FAILED'])+' fallidos, '+str(results['counts']['BLOCKED'])+' bloqueados y '+str(results['counts']['SKIPPED'])+' omitidos. El servidor Ollama se inició solo durante la comprobación y se cerró al finalizar.')
for result in results['results']:
    p=doc.add_paragraph(result['name']+'  '+result['status']+'  '+str(result['seconds'])+' s')
    p.paragraph_format.first_line_indent=Inches(0);p.paragraph_format.line_spacing=1
doc.add_paragraph('Fuente: artifacts/etapas56-20261029/results.json. Las duraciones corresponden a la ejecución registrada, no a garantías de rendimiento.')
# Real unmodified framebuffer captures. Originals remain available at full resolution.
for name,caption in [
 ('memoria-antes.png','Anexo B Estadísticas iniciales de memoria'),
 ('memoria-despues.png','Anexo C Asignación y recuperación de memoria'),
 ('procesos-hilos.png','Anexo D Procesos e hilos en espera con la consola activa'),
 ('round-robin-recuperacion.png','Anexo E Alternancia y recursos de hilos terminados'),
 ('sistema-despues.png','Anexo F Funcionamiento posterior y texto español')]:
    doc.add_page_break();doc.add_heading(caption,1)
    picture(EV/'evidencias'/name,'Captura real de QEMU obtenida mediante QMP')
    doc.add_paragraph('El registro serie completo permite consultar el texto que aparece dividido en varias líneas por el ancho de la consola. La captura no es una simulación ni se ha reemplazado su contenido.')
# Remove inherited theme fonts and paragraph rules from the default Word template.
for st in doc.styles:
    for border in list(st.element.xpath('.//w:pBdr')):
        border.getparent().remove(border)
    if st.type==1:
        fonts=st.element.get_or_add_rPr().get_or_add_rFonts()
        for attr in list(fonts.attrib):
            if attr.endswith('Theme'):del fonts.attrib[attr]
        for attr in ('ascii','hAnsi','eastAsia','cs'):fonts.set(qn('w:'+attr),'Times New Roman')
for p in doc.paragraphs:
    for border in list(p._p.xpath('.//w:pBdr')):border.getparent().remove(border)
out=ROOT/'docs/informes/INFORME_ACADEMICO_ETAPAS56.docx';doc.save(out)
print(out)
