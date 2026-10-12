#include "model/DrawingDetails.h"
#include "model/Document.h"
#include <QSet>
#include <QHash>
#include <algorithm>

namespace Loopwire {
QString drawingFieldKey(const QString& key) {
    if(key.compare(QLatin1String("DDpntxt"),Qt::CaseInsensitive)==0)return QStringLiteral("drawingNo");
    if(key.compare(QLatin1String("DDrvtxt"),Qt::CaseInsensitive)==0)return QStringLiteral("revision");
    if(key.compare(QLatin1String("DDtltxt"),Qt::CaseInsensitive)==0)return QStringLiteral("title");
    if(key==QLatin1String("drawingNo")||key==QLatin1String("drawingPn"))return QStringLiteral("drawingNo");
    if(key==QLatin1String("title")||key==QLatin1String("drawingName"))return QStringLiteral("title");
    if(key==QLatin1String("revision")||key==QLatin1String("drawingRevision")||key==QLatin1String("rev"))return QStringLiteral("revision");
    return {};
}
QString drawingFieldLabel(const QString& key) {
    return key==QLatin1String("drawingNo")?QStringLiteral("Drawing P/N"):key==QLatin1String("title")?QStringLiteral("Title"):QStringLiteral("Revision");
}
QString drawingFieldTextType(const QString& key) {
    const QString field=drawingFieldKey(key);
    if(field==QLatin1String("drawingNo"))return QStringLiteral("DDpntxt");
    if(field==QLatin1String("revision"))return QStringLiteral("DDrvtxt");
    if(field==QLatin1String("title"))return QStringLiteral("DDtltxt");
    return {};
}
namespace {
struct Field { const Entity* entity; QString key, value, layout; };
std::vector<Field> fields(const Document& doc) {
    std::vector<Field> result;
    auto collect=[&](const std::vector<Entity>& entities,const QString& layout){
        for(const Entity& e:entities){const QString key=drawingFieldKey(e.meta.value(QStringLiteral("field")).toString());
            if(!key.isEmpty())result.push_back({&e,key,e.meta.value(QStringLiteral("text")).toString().trimmed(),layout});}
    };
    collect(doc.entities(),{});for(const DrawingLayout& layout:doc.layouts())collect(layout.paperEntities,layout.name);
    return result;
}
QString filledValue(const Field& field) {
    const QString caption=field.entity->meta.value(QStringLiteral("fieldLabel")).toString().trimmed();
    return !caption.isEmpty()&&caption==field.value&&!field.entity->meta.value(QStringLiteral("drawingDetailField")).toBool()?QString():field.value;
}
}
QJsonObject effectiveDrawingDetails(const Document& doc) {
    QJsonObject result=doc.drawingDetails();
    QHash<QString,QSet<QString>> values;
    for(const Field& field:fields(doc)){const QString value=filledValue(field);if(!value.isEmpty())values[field.key].insert(value);}
    for(const QString& key:{QStringLiteral("drawingNo"),QStringLiteral("revision"),QStringLiteral("title")})
        if(!result.contains(key)&&values.value(key).size()==1)result[key]=*values.value(key).begin();
    return result;
}
std::vector<DrawingFieldIssue> checkDrawingDetails(const Document& doc) {
    std::vector<DrawingFieldIssue> result;const auto all=fields(doc);const QJsonObject declared=doc.drawingDetails();
    for(const QString& key:{QStringLiteral("drawingNo"),QStringLiteral("revision"),QStringLiteral("title")}){
        QSet<QString> values;int copies=0;
        for(const Field& field:all)if(field.key==key){values.insert(filledValue(field));++copies;}
        const bool authoritative=declared.contains(key);const QString expected=declared.value(key).toString().trimmed();
        if(authoritative&&expected.isEmpty()&&copies==0)
            result.push_back({QStringLiteral("drawing-fields:%1:document").arg(key),key,QStringLiteral("%1 is not filled in Drawing details").arg(drawingFieldLabel(key)),{},{},{}});
        for(const Field& field:all){
            if(field.key!=key)continue;
            const QString actual=authoritative?field.value:filledValue(field);
            const bool wrong=authoritative?(expected.isEmpty()||actual!=expected):values.size()>1;
            if(!wrong)continue;
            const QString where=field.layout.isEmpty()?QStringLiteral("Model"):field.layout;
            const QString message=authoritative?QStringLiteral("%1 on %2 is '%3'; Drawing details requires '%4'").arg(drawingFieldLabel(key),where,actual,expected)
                                               :QStringLiteral("%1 copies disagree on %2; every copy must have the same value").arg(drawingFieldLabel(key),where);
            result.push_back({QStringLiteral("drawing-fields:%1:%2:%3").arg(key,where,field.entity->id),key,message,field.entity->id,field.layout,field.entity->a});
        }
    }
    return result;
}
int updateDrawingFields(Document& doc,const QJsonObject& details) {
    int count=0;
    auto update=[&](std::vector<Entity>& entities){for(Entity& e:entities){const QString key=drawingFieldKey(e.meta.value(QStringLiteral("field")).toString());
        if(!key.isEmpty()&&details.contains(key)){const QString value=details.value(key).toString().trimmed();if(e.meta.value(QStringLiteral("text")).toString()!=value){e.meta[QStringLiteral("text")]=value;++count;}}}};
    update(doc.entities());for(DrawingLayout& layout:doc.layouts())update(layout.paperEntities);return count;
}
}
