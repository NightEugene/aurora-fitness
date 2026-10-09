// SPDX-License-Identifier: BSD-3-Clause
// Изолированные проверки релизных дефектов; БД создаётся во временном HOME.
#include "app/src/storage.h"
#include "app/src/appsettings.h"
#include <QFile>
#include "app/src/xiaomi/crypto.h"
#include "app/src/xiaomi/activityparser.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QDateTime>
#include <QSqlQuery>
#include <cassert>
#include <cstdio>
int failures = 0;
void check(bool ok, const char *name) {
 std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
 if (!ok) ++failures;
}
int main(int argc, char **argv) {
 QTemporaryDir home;
 assert(home.isValid());
 qputenv("HOME", home.path().toUtf8());
 QCoreApplication app(argc, argv);
 Storage s;
 const qlonglong ts = QDateTime::currentDateTime().toTime_t();
 QVariantMap summary{{"kind","dailySummary"},{"timestamp",ts},{"steps",1234},{"calories",56}};
 s.saveParsed(summary);
 assert(s.todaySummary().value("steps").toInt()==1234);
 s.saveParsed(QVariantMap{{"kind","dailySummary"},{"timestamp",ts}});
 check(s.todaySummary().value("steps").toInt()==1234, "empty summary preserves existing steps");
 QVariantList stages{QVariantMap{{"ts",ts},{"stage","light"}}};
 QVariantMap sleep{{"kind","sleep"},{"bedTime",ts},{"wakeTime",ts+3600},{"sleepMin",60},
   {"summary",QVariantMap{{"lightMin",60}}},{"stages",stages}};
 s.saveParsed(sleep);
 assert(s.sleepStages(ts).size()==1);
 {
 QSqlDatabase db=QSqlDatabase::database(QSqlDatabase::connectionNames().first());
 QSqlQuery q(db);
 assert(q.exec("CREATE TRIGGER reject_stage BEFORE INSERT ON sleep_stages BEGIN SELECT RAISE(ABORT, 'audit failure'); END"));
 sleep["sleepMin"]=30;
 s.saveParsed(sleep);
 check(s.sleepStages(ts).size()==1 && s.sleepSessions(1).first().toMap().value("sleepMin").toInt()==60,
       "failed stage write rolls back the complete sleep session");
 }
 const QByteArray encrypted=xcrypto::aesCcmEncrypt(QByteArray(16,'k'),QByteArray(13,'n'),QByteArray(65536,'p'));
 bool ok=true;
 xcrypto::aesCcmDecrypt(QByteArray(16,'k'),QByteArray(13,'n'),encrypted,&ok);
 check(encrypted.isEmpty(), "CCM encryption reports an OpenSSL failure");
 xiaomiactivity::FileId id;
 id.valid=true; id.type=0; id.subtype=8; id.version=2;
 QByteArray payload(10,0);
 payload[0]=char(8); // declares HR block; block bytes are missing
 payload[2]=char(1); // nonzero bedTime
 const QVariantMap parsed=xiaomiactivity::parseActivityFile(id,payload);
 check(parsed.value("kind").toString()=="unknown", "truncated sleep payload is rejected");
 // Ошибки БД должны доходить до загрузчика, а неизвестный файл — попадать в архив.
 check(!s.saveParsed(sleep), "storage reports failed transaction");
 const QByteArray raw(32, 'a');
 check(!s.saveActivityFile(raw, sleep), "database failure prevents file confirmation");
 const QVariantMap unknown{{"kind", "unknown"}};
 check(s.saveActivityFile(raw, unknown), "unknown format is durably archived");
 check(s.saveActivityFile(raw, unknown), "repeated file is archived idempotently");
 const QString archive = appConfigDir() + "/activity";
 assert(QDir(archive).removeRecursively());
 QFile blocked(archive);
 assert(blocked.open(QIODevice::WriteOnly));
 blocked.close();
 check(!s.saveActivityFile(raw, unknown), "archive failure prevents file confirmation");
 assert(blocked.remove());
 int changes = 0;
 QObject::connect(&s, &Storage::dataChanged, [&]() { ++changes; });
 s.saveBattery(0, 0);
 check(changes == 1 && !s.batteryHistory(1).isEmpty()
       && s.batteryHistory(1).last().toMap().value("level").toInt() == 0,
       "zero battery is stored and notifies the interface");
 // Открытие второй копии Storage не должно повторно переписывать timestamps.
 {
 QSqlDatabase db=QSqlDatabase::database(QSqlDatabase::connectionNames().first());
 QSqlQuery q(db);
 assert(q.exec("INSERT INTO daily_summary(ts,steps) VALUES(1000000001,42)"));
 Storage another;
 assert(q.exec("SELECT steps FROM daily_summary WHERE ts=1000000001"));
 check(q.next() && q.value(0).toInt()==42, "schema migration is not repeated by GUI connection");
 }
 return failures ? 1 : 0;
}
