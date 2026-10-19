#include "core/service/ParkingService.h"
#include "core/service/ReservationService.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <set>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace smartpark;
namespace{
int failures = 0;
void expect(bool value, const char *message){
    if (!value){
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
ParkingLayout layout(){
    return ParkingLayout::fromDescription(
        "site 90 60\nentrance 0 8\nentrance 0 54\nexit 90 8\nexit 90 54\n"
        "region A 25 4 3 1 1.2 5.5 6 left\n"
        "region B 25 38 3 1 1.2 5.5 6 left\n");
}
void runUntil(QEventLoop &loop, const std::function<bool()> &done){
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    if (!done()){
        loop.exec();
    }
    expect(done(), "asynchronous operation completes before timeout");
}
void testQueueAndResponsiveness(){
    ParkingService service(layout());
    QObject context;
    QEventLoop loop;
    int completed = 0, accepted = 0, busy = 0, ticks = 0;
    std::set<std::string> assigned;
    QTimer heartbeat;
    heartbeat.setInterval(0);
    QObject::connect(&heartbeat, &QTimer::timeout, &context, [&]{ ++ticks; });
    heartbeat.start();
    for (int i = 0; i < 9; ++i){
        service.enterAsync({"QUEUE-" + std::to_string(i), VehicleType::Car}, &context,
            [&](AsyncEntryOutcome result){
            expect(QThread::currentThread() == context.thread(), "commit callback uses owner thread");
            if (result.allocation){
                ++accepted;
                expect(assigned.insert(result.allocation->spotId).second,
                       "concurrent requests never occupy the same spot");
            }
            busy += result.busy;
            if (++completed == 9){ loop.quit(); }
        });
    }
    expect(completed == 0 && service.occupiedSpots() == 0,
           "submission returns without performing allocation or callback inline");
    runUntil(loop, [&]{ return completed == 9; });
    expect(accepted == 6 && busy == 1 && service.occupiedSpots() == 6,
           "bounded queue rejects ninth request and allocates six physical spots once");
    expect(ticks > 0, "event loop continues pumping heartbeat while searching");
}
void testStateChangeAndDuplicate(){
    ParkingService service(layout());
    QObject context;
    QEventLoop loop;
    int completed = 0, accepted = 0;
    const auto expected = service.previewAllocation({"BLOCKER", VehicleType::Car}, service.strategy());
    expect(expected.has_value(), "reference allocation is reachable");
    service.enterAsync({"ASYNC", VehicleType::Car}, &context, [&](AsyncEntryOutcome result){
        ++completed;
        accepted += result.allocation.has_value();
        expect(result.allocation && result.allocation->strategy == AllocationStrategy::Nearest,
               "strategy changed during search is recomputed before commit");
        expect(result.allocation && expected && result.allocation->spotId != expected->spotId,
               "spot occupied after snapshot is not assigned using stale proposal");
        if (completed == 2){ loop.quit(); }
    });
    service.enterAsync({"ASYNC", VehicleType::Car}, &context, [&](AsyncEntryOutcome result){
        ++completed;
        accepted += result.allocation.has_value();
        expect(!result.allocation, "duplicate plate queued during planning is rejected");
        if (completed == 2){ loop.quit(); }
    });
    QTimer::singleShot(0, &context, [&]{
        expect(service.enter({"BLOCKER", VehicleType::Car}).has_value(), "competing entry commits");
        service.setStrategy(AllocationStrategy::Nearest);
    });
    runUntil(loop, [&]{ return completed == 2; });
    expect(accepted == 1 && service.occupiedSpots() == 2, "only blocker and one queued car enter");
}
void testContextAndServiceDestruction(){
    QObject context;
    QEventLoop loop;
    auto service = std::make_unique<ParkingService>(layout());
    bool callback = false, destroyed = false;
    service->enterAsync({"TEARDOWN", VehicleType::Car}, &context,
                        [&](AsyncEntryOutcome){ callback = true; });
    QTimer::singleShot(0, &context, [&]{
        service.reset();
        destroyed = true;
        loop.quit();
    });
    runUntil(loop, [&]{ return destroyed; });
    expect(!callback, "service teardown waits for worker without delivering a stale callback");

    ParkingService survivor(layout());
    auto cancelledContext = std::make_unique<QObject>();
    survivor.enterAsync({"CANCELLED", VehicleType::Car}, cancelledContext.get(),
                         [&](AsyncEntryOutcome){ callback = true; });
    cancelledContext.reset();
    bool sentinel = false;
    survivor.enterAsync({"SURVIVOR", VehicleType::Car}, &context, [&](AsyncEntryOutcome result){
        expect(result.allocation.has_value(), "queue progresses past destroyed request context");
        sentinel = true;
        loop.quit();
    });
    runUntil(loop, [&]{ return sentinel; });
    expect(!callback && !survivor.activeRecord("CANCELLED"), "cancelled context never commits entry");
}
void testReservationArrival(){
    ParkingService service(layout());
    QObject context;
    QEventLoop loop;
    const auto now = ParkingRecord::Clock::now();
    const auto start = now + std::chrono::minutes(30);
    const auto reservation = service.reservations().create(
        {"RESERVED", VehicleType::Electric}, start, start + std::chrono::hours(1), now);
    expect(reservation.has_value(), "reservation created for asynchronous arrival");
    if (!reservation){ return; }
    bool done = false;
    service.enterAsync({"RESERVED", VehicleType::Car}, &context, [&](AsyncEntryOutcome result){
        expect(result.allocation && result.allocation->spotId == reservation->reservation.spotId(),
               "asynchronous arrival retains reserved spot and reservation transaction");
        const auto record = service.activeRecord("RESERVED");
        expect(record && record->vehicleType() == VehicleType::Electric,
               "reservation vehicle type is preserved instead of request type");
        expect(service.reservations().findCheckedIn("RESERVED").has_value(),
               "asynchronous reservation check-in advances state machine");
        done = true;
        loop.quit();
    });
    runUntil(loop, [&]{ return done; });
}
bool samePoints(const Route &a, const Route &b){
    if (a.points.size() != b.points.size()){ return false; }
    for (std::size_t i = 0; i < a.points.size(); ++i){
        if (a.points[i].x != b.points[i].x || a.points[i].y != b.points[i].y){ return false; }
    }
    return true;
}
void testParallelDeterminism(){
    ParkingService service(layout());
    const Vehicle vehicle("DETERMINISTIC", VehicleType::Electric);
    for (const auto strategy : {AllocationStrategy::WeightedCost, AllocationStrategy::Nearest}){
#ifdef _OPENMP
        omp_set_num_threads(1);
#endif
        const auto serial = service.previewAllocation(vehicle, strategy);
#ifdef _OPENMP
        omp_set_num_threads(4);
#endif
        const auto parallel = service.previewAllocation(vehicle, strategy);
        expect(serial && parallel && serial->spotId == parallel->spotId
               && serial->score == parallel->score
               && serial->entranceIndex == parallel->entranceIndex
               && serial->exitIndex == parallel->exitIndex
               && samePoints(serial->entryRoute, parallel->entryRoute)
               && samePoints(serial->exitRoute, parallel->exitRoute),
               "one-thread and multi-thread planning retain identical selection, routes and score");
    }
}
} // namespace
int main(int argc, char **argv){
    QCoreApplication app(argc, argv);
    testParallelDeterminism();
    testQueueAndResponsiveness();
    testStateChangeAndDuplicate();
    testContextAndServiceDestruction();
    testReservationArrival();
    std::cout << "Background planning failures: " << failures << '\n';
    return failures ? 1 : 0;
}
