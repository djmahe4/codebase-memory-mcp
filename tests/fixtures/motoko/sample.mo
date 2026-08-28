actor Counter {
  var count : Nat = 0;
  public func get() : async Nat { count };
}
